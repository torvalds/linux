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

/* Developed initially by Nick Kew and Chris Darroch.
 * Contributed to the APR project by kind permission of
 * Pearson Education Core Technology Group (CTG),
 * formerly Central Media Group (CMG).
 */

/* apr_dbd_oracle - a painful attempt
 *
 * Based first on the documentation at
 * http://download-west.oracle.com/docs/cd/B10501_01/appdev.920/a96584/toc.htm
 *
 * Those docs have a lot of internal inconsistencies, contradictions, etc
 * So I've snarfed the demo programs (from Oracle 8, not included in
 * the current downloadable oracle), and used code from them.
 *
 * Why do cdemo81.c and cdemo82.c do the same thing in very different ways?
 * e.g. cdemo82 releases all its handle on shutdown; cdemo81 doesn't
 *
 * All the ORA* functions return a "sword".  Some of them are documented;
 * others aren't.  So I've adopted a policy of using switch statements
 * everywhere, even when we're not doing anything with the return values.
 *
 * This makes no attempt at performance tuning, such as setting
 * prefetch cache size.  We need some actual performance data
 * to make that meaningful.  Input from someone with experience
 * as a sysop using oracle would be a good start.
 */

/* shut compiler up */
#ifdef DEBUG
#define int_errorcode int errorcode
#else
#define int_errorcode
#endif

#include "apu.h"

#if APU_HAVE_ORACLE

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include <oci.h>

#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_time.h"
#include "apr_hash.h"
#include "apr_buckets.h"

#define TRANS_TIMEOUT 30
#define MAX_ARG_LEN 256 /* in line with other apr_dbd drivers.  We alloc this
                         * lots of times, so a large value gets hungry.
                         * Should really make it configurable
                         */
#define DEFAULT_LONG_SIZE 4096
#define DBD_ORACLE_MAX_COLUMNS 256
#define NUMERIC_FIELD_SIZE 32

#define CHECK_CONN_QUERY "SELECT 1 FROM dual"

#define ERR_BUF_SIZE 200

#ifdef DEBUG
#include <stdio.h>
#endif

#include "apr_dbd_internal.h"

/* declarations */
static const char *dbd_oracle_error(apr_dbd_t *sql, int n);
static int dbd_oracle_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                              const char *query, const char *label,
                              int nargs, int nvals, apr_dbd_type_e *types,
                              apr_dbd_prepared_t **statement);
static int outputParams(apr_dbd_t*, apr_dbd_prepared_t*);
static int dbd_oracle_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **results,
                              apr_dbd_prepared_t *statement,
                              int seek, const char **values);
static int dbd_oracle_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                             int *nrows, apr_dbd_prepared_t *statement,
                             const char **values);
static int dbd_oracle_start_transaction(apr_pool_t *pool, apr_dbd_t *sql,
                                        apr_dbd_transaction_t **trans);
static int dbd_oracle_end_transaction(apr_dbd_transaction_t *trans);

struct apr_dbd_transaction_t {
    int mode;
    enum { TRANS_NONE, TRANS_ERROR, TRANS_1, TRANS_2 } status;
    apr_dbd_t *handle;
    OCITrans *trans;
    OCISnapshot *snapshot1;
    OCISnapshot *snapshot2;
};

struct apr_dbd_results_t {
    apr_pool_t *pool;
    apr_dbd_t* handle;
    unsigned int rownum;
    int seek;
    int nrows;
    apr_dbd_prepared_t *statement;
};

struct apr_dbd_t {
    sword status;
    OCIError *err;
    OCIServer *svr;
    OCISvcCtx *svc;
    OCISession *auth;
    apr_dbd_transaction_t* trans;
    apr_pool_t *pool;
    char buf[ERR_BUF_SIZE]; /* for error messages */
    apr_size_t long_size;
    apr_dbd_prepared_t *check_conn_stmt;
};

struct apr_dbd_row_t {
    int n;
    apr_dbd_results_t *res;
    apr_pool_t *pool;
};

typedef struct {
    apr_dbd_type_e type;
    sb2 ind;
    sb4 len;
    OCIBind *bind;
    union {
        void *raw;
        char *sval;
        int ival;
        unsigned int uval;
        double fval;
        OCILobLocator *lobval;
    } value;
} bind_arg;

typedef struct {
    int type;
    sb2 ind;
    ub2 len;         /* length of actual output */
    OCIDefine *defn;
    apr_size_t sz;   /* length of buf for output */
    union {
        void *raw;
        char *sval;
        OCILobLocator *lobval;
    } buf;
    const char *name;
} define_arg;

struct apr_dbd_prepared_t {
    OCIStmt *stmt;
    int nargs;
    int nvals;
    bind_arg *args;
    int nout;
    define_arg *out;
    apr_dbd_t *handle;
    apr_pool_t *pool;
    ub2 type;
};

/* AFAICT from the docs, the OCIEnv thingey can be used async
 * across threads, so lets have a global one.
 *
 * We'll need shorter-lived envs to deal with requests and connections
 *
 * Hmmm, that doesn't work: we don't have a usermem framework.
 * OK, forget about using APR pools here, until we figure out
 * the right way to do it (if such a thing exists).
 */
static OCIEnv *dbd_oracle_env = NULL;

/* Oracle specific bucket for BLOB/CLOB types */
typedef struct apr_bucket_lob apr_bucket_lob;
/**
 * A bucket referring to a Oracle BLOB/CLOB
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
    apr_size_t blength = e->length;  /* bytes remaining in file past offset */
    apr_off_t boffset = e->start;
    define_arg *val = &res->statement->out[col];
    apr_dbd_t *sql = res->handle;
/* Only with 10g, unfortunately
    oraub8 length = APR_BUCKET_BUFF_SIZE;
*/
    ub4 length = APR_BUCKET_BUFF_SIZE;
    char *buf = NULL;

    *str = NULL;  /* in case we die prematurely */

    /* fetch from offset if not at the beginning */
    buf = apr_palloc(row->pool, APR_BUCKET_BUFF_SIZE);
    sql->status = OCILobRead(sql->svc, sql->err, val->buf.lobval,
                             &length, 1 + (size_t)boffset,
                             (dvoid*) buf, APR_BUCKET_BUFF_SIZE,
                             NULL, NULL, 0, SQLCS_IMPLICIT);
/* Only with 10g, unfortunately
    sql->status = OCILobRead2(sql->svc, sql->err, val->buf.lobval,
                              &length, NULL, 1 + boffset,
                              (dvoid*) buf, APR_BUCKET_BUFF_SIZE,
                              OCI_ONE_PIECE, NULL, NULL, 0, SQLCS_IMPLICIT);
*/
    if (sql->status != OCI_SUCCESS) {
        return APR_EGENERAL;
    }
    blength -= length;
    *len = length;
    *str = buf;

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

static apr_status_t dbd_free_lobdesc(void *lob)
{
    switch (OCIDescriptorFree(lob, OCI_DTYPE_LOB)) {
    case OCI_SUCCESS:
        return APR_SUCCESS;
    default:
        return APR_EGENERAL;
    }
}

static apr_status_t dbd_free_snapshot(void *snap)
{
    switch (OCIDescriptorFree(snap, OCI_DTYPE_SNAP)) {
    case OCI_SUCCESS:
        return APR_SUCCESS;
    default:
        return APR_EGENERAL;
    }
}

static void dbd_oracle_init(apr_pool_t *pool)
{
    if (dbd_oracle_env == NULL) {
        /* Sadly, OCI_SHARED seems to be impossible to use, due to
         * various Oracle bugs.  See, for example, Oracle MetaLink bug 2972890
         * and PHP bug http://bugs.php.net/bug.php?id=23733
         */
#ifdef OCI_NEW_LENGTH_SEMANTICS
        OCIEnvCreate(&dbd_oracle_env, OCI_THREADED|OCI_NEW_LENGTH_SEMANTICS,
                     NULL, NULL, NULL, NULL, 0, NULL);
#else
        OCIEnvCreate(&dbd_oracle_env, OCI_THREADED,
                     NULL, NULL, NULL, NULL, 0, NULL);
#endif
    }
}

static apr_dbd_t *dbd_oracle_open(apr_pool_t *pool, const char *params,
                                  const char **error)
{
    apr_dbd_t *ret = apr_pcalloc(pool, sizeof(apr_dbd_t));
    int errorcode;

    char *BLANK = "";
    struct {
        const char *field;
        char *value;
    } fields[] = {
        {"user", BLANK},
        {"pass", BLANK},
        {"dbname", BLANK},
        {"server", BLANK},
        {NULL, NULL}
    };
    int i;
    const char *ptr;
    const char *key;
    size_t klen;
    const char *value;
    size_t vlen;
    static const char *const delims = " \r\n\t;|,";

    ret->pool = pool;
    ret->long_size = DEFAULT_LONG_SIZE;

    /* snitch parsing from the MySQL driver */
    for (ptr = strchr(params, '='); ptr; ptr = strchr(ptr, '=')) {
        /* don't dereference memory that may not belong to us */
        if (ptr == params) {
            ++ptr;
            continue;
        }
        for (key = ptr-1; apr_isspace(*key); --key);
        klen = 0;
        while (apr_isalpha(*key)) {
            if (key == params) {
                /* Don't parse off the front of the params */
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
        for (i=0; fields[i].field != NULL; ++i) {
            if (!strncasecmp(fields[i].field, key, klen)) {
                fields[i].value = apr_pstrndup(pool, value, vlen);
                break;
            }
        }
        ptr = value+vlen;
    }

    ret->status = OCIHandleAlloc(dbd_oracle_env, (dvoid**)&ret->err,
                                 OCI_HTYPE_ERROR, 0, NULL);
    switch (ret->status) {
    default:
#ifdef DEBUG
        printf("ret->status is %d\n", ret->status);
        break;
#else
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }

    ret->status = OCIHandleAlloc(dbd_oracle_env, (dvoid**)&ret->svr,
                                 OCI_HTYPE_SERVER, 0, NULL);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (alloc svr): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }

    ret->status = OCIHandleAlloc(dbd_oracle_env, (dvoid**)&ret->svc,
                                 OCI_HTYPE_SVCCTX, 0, NULL);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (alloc svc): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }

/* All the examples use the #else */
#if CAN_DO_LOGIN
    ret->status = OCILogon(dbd_oracle_env, ret->err, &ret->svc, fields[0].value,
                     strlen(fields[0].value), fields[1].value,
                     strlen(fields[1].value), fields[2].value,
                     strlen(fields[2].value));
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR: %s\n", ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
#else
    ret->status = OCIServerAttach(ret->svr, ret->err, (text*) fields[3].value,
                                  strlen(fields[3].value), OCI_DEFAULT);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (server attach): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCIAttrSet(ret->svc, OCI_HTYPE_SVCCTX, ret->svr, 0,
                        OCI_ATTR_SERVER, ret->err);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (attr set): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCIHandleAlloc(dbd_oracle_env, (dvoid**)&ret->auth,
                            OCI_HTYPE_SESSION, 0, NULL);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (alloc auth): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCIAttrSet(ret->auth, OCI_HTYPE_SESSION, fields[0].value,
                        strlen(fields[0].value), OCI_ATTR_USERNAME, ret->err);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (attr username): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCIAttrSet(ret->auth, OCI_HTYPE_SESSION, fields[1].value,
                        strlen(fields[1].value), OCI_ATTR_PASSWORD, ret->err);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (attr password): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCISessionBegin(ret->svc, ret->err, ret->auth,
                             OCI_CRED_RDBMS, OCI_DEFAULT);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (session begin): %s\n", ret->status, ret->buf);
        break;
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
    case OCI_SUCCESS:
        break;
    }
    ret->status = OCIAttrSet(ret->svc, OCI_HTYPE_SVCCTX, ret->auth, 0,
                        OCI_ATTR_SESSION, ret->err);
    switch (ret->status) {
    default:
#ifdef DEBUG
        OCIErrorGet(ret->err, 1, NULL, &errorcode, ret->buf,
                    sizeof(ret->buf), OCI_HTYPE_ERROR);
        printf("OPEN ERROR %d (attr session): %s\n", ret->status, ret->buf);
#else
        if (error) {
            *error = apr_pcalloc(pool, ERR_BUF_SIZE);
            OCIErrorGet(ret->err, 1, NULL, &errorcode, (unsigned char*)(*error),
                        ERR_BUF_SIZE, OCI_HTYPE_ERROR);
        }
        return NULL;
#endif
        break;
    case OCI_SUCCESS:
        break;
    }
#endif

    if(dbd_oracle_prepare(pool, ret, CHECK_CONN_QUERY, NULL, 0, 0, NULL,
                          &ret->check_conn_stmt) != 0) {
        return NULL;
    }

    return ret;
}

#ifdef EXPORT_NATIVE_FUNCS
static apr_size_t dbd_oracle_long_size_set(apr_dbd_t *sql,
                                           apr_size_t long_size)
{
    apr_size_t old_size = sql->long_size;
    sql->long_size = long_size;
    return old_size;
}
#endif

static const char *dbd_oracle_get_name(const apr_dbd_results_t *res, int n)
{
    define_arg *val = &res->statement->out[n];

    if ((n < 0) || (n >= res->statement->nout)) {
        return NULL;
    }
    return val->name;
}

static int dbd_oracle_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                              apr_dbd_row_t **rowp, int rownum)
{
    apr_dbd_row_t *row = *rowp;
    apr_dbd_t *sql = res->handle;
    int_errorcode;

    if (row == NULL) {
        row = apr_palloc(pool, sizeof(apr_dbd_row_t));
        *rowp = row;
        row->res = res;
        /* Oracle starts counting at 1 according to the docs */
        row->n = res->seek ? rownum : 1;
        row->pool = pool;
    }
    else {
        if (res->seek) {
            row->n = rownum;
        }
        else {
            ++row->n;
        }
    }

    if (res->seek) {
        sql->status = OCIStmtFetch2(res->statement->stmt, res->handle->err, 1,
                                    OCI_FETCH_ABSOLUTE, row->n, OCI_DEFAULT);
    }
    else {
        sql->status = OCIStmtFetch2(res->statement->stmt, res->handle->err, 1,
                                    OCI_FETCH_NEXT, 0, OCI_DEFAULT);
    }
    switch (sql->status) {
    case OCI_SUCCESS:
        (*rowp)->res = res;
        return 0;
    case OCI_NO_DATA:
        return -1;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Execute error %d: %s\n", sql->status, sql->buf);
#endif
        /* fallthrough */
    default:
        return 1;
    }
    return 0;
}

static const char *dbd_oracle_error(apr_dbd_t *sql, int n)
{
    /* This is ugly.  Needs us to pass in a buffer of unknown size.
     * Either we put it on the handle, or we have to keep allocing/copying
     */
    sb4 errorcode;

    switch (sql->status) {
    case OCI_SUCCESS:
        return "OCI_SUCCESS";
    case OCI_SUCCESS_WITH_INFO:
        return "OCI_SUCCESS_WITH_INFO";
    case OCI_NEED_DATA:
        return "OCI_NEED_DATA";
    case OCI_NO_DATA:
        return "OCI_NO_DATA";
    case OCI_INVALID_HANDLE:
        return "OCI_INVALID_HANDLE";
    case OCI_STILL_EXECUTING:
        return "OCI_STILL_EXECUTING";
    case OCI_CONTINUE:
        return "OCI_CONTINUE";
    }

    switch (OCIErrorGet(sql->err, 1, NULL, &errorcode,
                        (text*) sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR)) {
    case OCI_SUCCESS:
        return sql->buf; 
    default:
        return "internal error: OCIErrorGet failed";
    }
}

static apr_status_t freeStatement(void *statement)
{
    int rv = APR_SUCCESS;
    OCIStmt *stmt = ((apr_dbd_prepared_t*)statement)->stmt;

#ifdef PREPARE2
    OCIError *err;

    if (OCIHandleAlloc(dbd_oracle_env, (dvoid**)&err, OCI_HTYPE_ERROR,
                       0, NULL) != OCI_SUCCESS) {
        return APR_EGENERAL;
    }
    if (OCIStmtRelease(stmt, err, NULL, 0, OCI_DEFAULT) != OCI_SUCCESS) {
        rv = APR_EGENERAL;
    }
    if (OCIHandleFree(err, OCI_HTYPE_ERROR) != OCI_SUCCESS) {
        rv = APR_EGENERAL;
    }
#else
    if (OCIHandleFree(stmt, OCI_HTYPE_STMT) != OCI_SUCCESS) {
        rv = APR_EGENERAL;
    }
#endif

    return rv;
}

static int dbd_oracle_select(apr_pool_t *pool, apr_dbd_t *sql,
                             apr_dbd_results_t **results,
                             const char *query, int seek)
{
    int ret = 0;
    apr_dbd_prepared_t *statement = NULL;

    ret = dbd_oracle_prepare(pool, sql, query, NULL, 0, 0, NULL, &statement);
    if (ret != 0) {
        return ret;
    }

    ret = dbd_oracle_pselect(pool, sql, results, statement, seek, NULL);
    if (ret != 0) {
        return ret;
    }

    return ret;
}

static int dbd_oracle_query(apr_dbd_t *sql, int *nrows, const char *query)
{
    int ret = 0;
    apr_pool_t *pool;
    apr_dbd_prepared_t *statement = NULL;

    if (sql->trans && sql->trans->status == TRANS_ERROR) {
        return 1;
    }

    /* make our own pool so that APR allocations don't linger and so that
     * both Stmt and LOB handles are cleaned up (LOB handles may be
     * allocated when preparing APR_DBD_TYPE_CLOB/BLOBs)
     */
    apr_pool_create(&pool, sql->pool);

    ret = dbd_oracle_prepare(pool, sql, query, NULL, 0, 0, NULL, &statement);
    if (ret == 0) {
        ret = dbd_oracle_pquery(pool, sql, nrows, statement, NULL);
        if (ret == 0) {
            sql->status = OCIAttrGet(statement->stmt, OCI_HTYPE_STMT,
                                     nrows, 0, OCI_ATTR_ROW_COUNT,
                                     sql->err);
        }
    }

    apr_pool_destroy(pool);

    return ret;
}

static const char *dbd_oracle_escape(apr_pool_t *pool, const char *arg,
                                     apr_dbd_t *sql)
{
    return arg;        /* OCI has no concept of string escape */
}

static int dbd_oracle_prepare(apr_pool_t *pool, apr_dbd_t *sql,
                              const char *query, const char *label,
                              int nargs, int nvals, apr_dbd_type_e *types,
                              apr_dbd_prepared_t **statement)
{
    int ret = 0;
    int i;
    apr_dbd_prepared_t *stmt ;

    if (*statement == NULL) {
        *statement = apr_pcalloc(pool, sizeof(apr_dbd_prepared_t));
    }
    stmt = *statement;
    stmt->handle = sql;
    stmt->pool = pool;
    stmt->nargs = nargs;
    stmt->nvals = nvals;

    /* populate our own args, if any */
    if (nargs > 0) {
        stmt->args = apr_pcalloc(pool, nargs*sizeof(bind_arg));
        for (i = 0; i < nargs; i++) {
            stmt->args[i].type = types[i];
        }
    }

    sql->status = OCIHandleAlloc(dbd_oracle_env, (dvoid**) &stmt->stmt,
                                 OCI_HTYPE_STMT, 0, NULL);
    if (sql->status != OCI_SUCCESS) {
        return 1;
    }

    sql->status = OCIStmtPrepare(stmt->stmt, sql->err, (text*) query,
                                 strlen(query), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (sql->status != OCI_SUCCESS) {
        OCIHandleFree(stmt->stmt, OCI_HTYPE_STMT);
        return 1;
    }

    apr_pool_cleanup_register(pool, stmt, freeStatement,
                              apr_pool_cleanup_null);

    /* Perl gets statement type here */
    sql->status = OCIAttrGet(stmt->stmt, OCI_HTYPE_STMT, &stmt->type, 0,
                             OCI_ATTR_STMT_TYPE, sql->err);
    if (sql->status != OCI_SUCCESS) {
        return 1;
    }

/* Perl sets PREFETCH_MEMORY here, but the docs say there's a working default */
#if 0
    sql->status = OCIAttrSet(stmt->stmt, OCI_HTYPE_STMT, &prefetch_size,
                             sizeof(prefetch_size), OCI_ATTR_PREFETCH_MEMORY,
                             sql->err);
    if (sql->status != OCI_SUCCESS) {
        return 1;
    }
#endif

    if (stmt->type == OCI_STMT_SELECT) {
        ret = outputParams(sql, stmt);
    }
    return ret;
}

static void dbd_oracle_bind(apr_dbd_prepared_t *statement, const char **values)
{
    OCIStmt *stmt = statement->stmt;
    apr_dbd_t *sql = statement->handle;
    int i, j;
    sb2 null_ind = -1;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        if (values[j] == NULL) {
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       NULL, 0, SQLT_STR,
                                       &null_ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
        }
        else {
            switch (statement->args[i].type) {
            case APR_DBD_TYPE_BLOB:
                {
                char *data = (char *)values[j];
                int size = atoi((char*)values[++j]);

                /* skip table and column for now */
                j += 2;

                sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                           sql->err, i + 1,
                                           data, size, SQLT_LBI,
                                           &statement->args[i].ind,
                                           NULL,
                                           (ub2) 0, (ub4) 0,
                                           (ub4 *) 0, OCI_DEFAULT);
                }
                break;
            case APR_DBD_TYPE_CLOB:
                {
                char *data = (char *)values[j];
                int size = atoi((char*)values[++j]);

                /* skip table and column for now */
                j += 2;

                sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                           sql->err, i + 1,
                                           data, size, SQLT_LNG,
                                           &statement->args[i].ind,
                                           NULL,
                                           (ub2) 0, (ub4) 0,
                                           (ub4 *) 0, OCI_DEFAULT);
                }
                break;
            default:
                sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                           sql->err, i + 1,
                                           (dvoid*) values[j],
                                           strlen(values[j]) + 1,
                                           SQLT_STR,
                                           &statement->args[i].ind,
                                           NULL,
                                           (ub2) 0, (ub4) 0,
                                           (ub4 *) 0, OCI_DEFAULT);
                break;
            }
        }

        if (sql->status != OCI_SUCCESS) {
            return;
        }
    }

    return;
}

static int outputParams(apr_dbd_t *sql, apr_dbd_prepared_t *stmt)
{
    OCIParam *parms;
    int i;
    ub2 paramtype[DBD_ORACLE_MAX_COLUMNS];
    ub2 paramsize[DBD_ORACLE_MAX_COLUMNS];
    char *paramname[DBD_ORACLE_MAX_COLUMNS];
    ub4 paramnamelen[DBD_ORACLE_MAX_COLUMNS];
    int_errorcode;

    /* Perl uses 0 where we used 1 */
    sql->status = OCIStmtExecute(sql->svc, stmt->stmt, sql->err, 0, 0,
                                 NULL, NULL, OCI_DESCRIBE_ONLY);
    switch (sql->status) {
    case OCI_SUCCESS:
    case OCI_SUCCESS_WITH_INFO:
        break;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Describing prepared statement: %s\n", sql->buf);
#endif
    default:
        return 1;
    }
    while (sql->status == OCI_SUCCESS) {
        sql->status = OCIParamGet(stmt->stmt, OCI_HTYPE_STMT,
                                  sql->err, (dvoid**)&parms, stmt->nout+1);
        switch (sql->status) {
        case OCI_SUCCESS:
            sql->status = OCIAttrGet(parms, OCI_DTYPE_PARAM,
                                     &paramtype[stmt->nout],
                                     0, OCI_ATTR_DATA_TYPE, sql->err);
            sql->status = OCIAttrGet(parms, OCI_DTYPE_PARAM,
                                     &paramsize[stmt->nout],
                                     0, OCI_ATTR_DATA_SIZE, sql->err);
            sql->status = OCIAttrGet(parms, OCI_DTYPE_PARAM,
                                     &paramname[stmt->nout],
                                     &paramnamelen[stmt->nout],
                                     OCI_ATTR_NAME, sql->err);
            ++stmt->nout;
        }
    }
    switch (sql->status) {
    case OCI_SUCCESS:
        break;
    case OCI_ERROR:
        break;        /* this is what we expect at end-of-loop */
    default:
        return 1;
    }

    /* OK, the above works.  We have the params; now OCIDefine them */
    stmt->out = apr_palloc(stmt->pool, stmt->nout*sizeof(define_arg));
    for (i=0; i<stmt->nout; ++i) {
        stmt->out[i].type = paramtype[i];
        stmt->out[i].len = stmt->out[i].sz = paramsize[i];
        stmt->out[i].name = apr_pstrmemdup(stmt->pool,
                                           paramname[i], paramnamelen[i]);
        switch (stmt->out[i].type) {
        default:
            switch (stmt->out[i].type) {
            case SQLT_NUM:           /* 2: numeric, Perl worst case=130+38+3 */
                stmt->out[i].sz = 171;
                break;
            case SQLT_CHR:           /* 1: char */
            case SQLT_AFC:           /* 96: ANSI fixed char */
                stmt->out[i].sz *= 4; /* ugh, wasteful UCS-4 handling */
                break;
            case SQLT_DAT:           /* 12: date, depends on NLS date format */
                stmt->out[i].sz = 75;
                break;
            case SQLT_BIN:           /* 23: raw binary, perhaps UTF-16? */
                stmt->out[i].sz *= 2;
                break;
            case SQLT_RID:           /* 11: rowid */
            case SQLT_RDD:           /* 104: rowid descriptor */
                stmt->out[i].sz = 20;
                break;
            case SQLT_TIMESTAMP:     /* 187: timestamp */
            case SQLT_TIMESTAMP_TZ:  /* 188: timestamp with time zone */
            case SQLT_INTERVAL_YM:   /* 189: interval year-to-month */
            case SQLT_INTERVAL_DS:   /* 190: interval day-to-second */
            case SQLT_TIMESTAMP_LTZ: /* 232: timestamp with local time zone */
                stmt->out[i].sz = 75;
                break;
            default:
#ifdef DEBUG
                printf("Unsupported data type: %d\n", stmt->out[i].type);
#endif
                break;
            }
            ++stmt->out[i].sz;
            stmt->out[i].buf.raw = apr_palloc(stmt->pool, stmt->out[i].sz);
            sql->status = OCIDefineByPos(stmt->stmt, &stmt->out[i].defn,
                                         sql->err, i+1,
                                         stmt->out[i].buf.sval,
                                         stmt->out[i].sz, SQLT_STR,
                                         &stmt->out[i].ind, &stmt->out[i].len,
                                         0, OCI_DEFAULT);
            break;
        case SQLT_LNG: /* 8: long */
            stmt->out[i].sz = sql->long_size * 4 + 4; /* ugh, UCS-4 handling */
            stmt->out[i].buf.raw = apr_palloc(stmt->pool, stmt->out[i].sz);
            sql->status = OCIDefineByPos(stmt->stmt, &stmt->out[i].defn,
                                         sql->err, i+1,
                                         stmt->out[i].buf.raw,
                                         stmt->out[i].sz, SQLT_LVC,
                                         &stmt->out[i].ind, NULL,
                                         0, OCI_DEFAULT);
            break;
        case SQLT_LBI: /* 24: long binary, perhaps UTF-16? */
            stmt->out[i].sz = sql->long_size * 2 + 4; /* room for int prefix */
            stmt->out[i].buf.raw = apr_palloc(stmt->pool, stmt->out[i].sz);
            sql->status = OCIDefineByPos(stmt->stmt, &stmt->out[i].defn,
                                         sql->err, i+1,
                                         stmt->out[i].buf.raw,
                                         stmt->out[i].sz, SQLT_LVB,
                                         &stmt->out[i].ind, NULL,
                                         0, OCI_DEFAULT);
            break;
        case SQLT_BLOB: /* 113 */
        case SQLT_CLOB: /* 112 */
/*http://download-west.oracle.com/docs/cd/B10501_01/appdev.920/a96584/oci05bnd.htm#434937*/
            sql->status = OCIDescriptorAlloc(dbd_oracle_env,
                                             (dvoid**)&stmt->out[i].buf.lobval,
                                             OCI_DTYPE_LOB, 0, NULL);
            apr_pool_cleanup_register(stmt->pool, stmt->out[i].buf.lobval,
                                      dbd_free_lobdesc,
                                      apr_pool_cleanup_null);
            sql->status = OCIDefineByPos(stmt->stmt, &stmt->out[i].defn,
                                         sql->err, i+1,
                                         (dvoid*) &stmt->out[i].buf.lobval,
                                         -1, stmt->out[i].type,
                                         &stmt->out[i].ind, &stmt->out[i].len,
                                         0, OCI_DEFAULT);
            break;
        }
        switch (sql->status) {
        case OCI_SUCCESS:
            break;
        default:
            return 1;
        }
    }
    return 0;
}

static int dbd_oracle_pquery(apr_pool_t *pool, apr_dbd_t *sql,
                             int *nrows, apr_dbd_prepared_t *statement,
                             const char **values)
{
    OCISnapshot *oldsnapshot = NULL;
    OCISnapshot *newsnapshot = NULL;
    apr_dbd_transaction_t* trans = sql->trans;
    int exec_mode;
    int_errorcode;

    if (trans) {
        switch (trans->status) {
        case TRANS_ERROR:
            return -1;
        case TRANS_NONE:
            trans = NULL;
            break;
        case TRANS_1:
            oldsnapshot = trans->snapshot1;
            newsnapshot = trans->snapshot2;
            trans->status = TRANS_2;
            break;
        case TRANS_2:
            oldsnapshot = trans->snapshot2;
            newsnapshot = trans->snapshot1;
            trans->status = TRANS_1;
            break;
        }
        exec_mode = OCI_DEFAULT;
    }
    else {
        exec_mode = OCI_COMMIT_ON_SUCCESS;
    }

    dbd_oracle_bind(statement, values);

    sql->status = OCIStmtExecute(sql->svc, statement->stmt, sql->err, 1, 0,
                                 oldsnapshot, newsnapshot, exec_mode);
    switch (sql->status) {
    case OCI_SUCCESS:
        break;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Execute error %d: %s\n", sql->status, sql->buf);
#endif
        /* fallthrough */
    default:
        if (TXN_NOTICE_ERRORS(trans)) {
            trans->status = TRANS_ERROR;
        }
        return 1;
    }

    sql->status = OCIAttrGet(statement->stmt, OCI_HTYPE_STMT, nrows, 0,
                             OCI_ATTR_ROW_COUNT, sql->err);
    return 0;
}

static int dbd_oracle_pvquery(apr_pool_t *pool, apr_dbd_t *sql,
                              int *nrows, apr_dbd_prepared_t *statement,
                              va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->status == TRANS_ERROR) {
        return -1;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const char*);
    }

    return dbd_oracle_pquery(pool, sql, nrows, statement, values);
}

static int dbd_oracle_pselect(apr_pool_t *pool, apr_dbd_t *sql,
                              apr_dbd_results_t **results,
                              apr_dbd_prepared_t *statement,
                              int seek, const char **values)
{
    int exec_mode = seek ? OCI_STMT_SCROLLABLE_READONLY : OCI_DEFAULT;
    OCISnapshot *oldsnapshot = NULL;
    OCISnapshot *newsnapshot = NULL;
    apr_dbd_transaction_t* trans = sql->trans;
    int_errorcode;

    if (trans) {
        switch (trans->status) {
        case TRANS_ERROR:
            return 1;
        case TRANS_NONE:
            trans = NULL;
            break;
        case TRANS_1:
            oldsnapshot = trans->snapshot1;
            newsnapshot = trans->snapshot2;
            trans->status = TRANS_2;
            break;
        case TRANS_2:
            oldsnapshot = trans->snapshot2;
            newsnapshot = trans->snapshot1;
            trans->status = TRANS_1;
            break;
        }
    }

    dbd_oracle_bind(statement, values);

    sql->status = OCIStmtExecute(sql->svc, statement->stmt, sql->err, 0, 0,
                                 oldsnapshot, newsnapshot, exec_mode);
    switch (sql->status) {
    case OCI_SUCCESS:
        break;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Executing prepared statement: %s\n", sql->buf);
#endif
        /* fallthrough */
    default:
        if (TXN_NOTICE_ERRORS(trans)) {
            trans->status = TRANS_ERROR;
        }
        return 1;
    }

    if (!*results) {
        *results = apr_palloc(pool, sizeof(apr_dbd_results_t));
    }
    (*results)->handle = sql;
    (*results)->statement = statement;
    (*results)->seek = seek;
    (*results)->rownum = seek ? 0 : -1;
    (*results)->pool = pool;

    return 0;
}

static int dbd_oracle_pvselect(apr_pool_t *pool, apr_dbd_t *sql,
                               apr_dbd_results_t **results,
                               apr_dbd_prepared_t *statement,
                               int seek, va_list args)
{
    const char **values;
    int i;

    if (sql->trans && sql->trans->status == TRANS_ERROR) {
        return -1;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const char*);
    }

    return dbd_oracle_pselect(pool, sql, results, statement, seek, values);
}

static void dbd_oracle_bbind(apr_dbd_prepared_t * statement,
                             const void **values)
{
    OCIStmt *stmt = statement->stmt;
    apr_dbd_t *sql = statement->handle;
    int i, j;
    sb2 null_ind = -1;
    apr_dbd_type_e type;

    for (i = 0, j = 0; i < statement->nargs; i++, j++) {
        type = (values[j] == NULL ? APR_DBD_TYPE_NULL
                                  : statement->args[i].type);

        switch (type) {
        case APR_DBD_TYPE_TINY:
            statement->args[i].value.ival = *(char*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.ival,
                                       sizeof(statement->args[i].value.ival),
                                       SQLT_INT,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_UTINY:
            statement->args[i].value.uval = *(unsigned char*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.uval,
                                       sizeof(statement->args[i].value.uval),
                                       SQLT_UIN,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_SHORT:
            statement->args[i].value.ival = *(short*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.ival,
                                       sizeof(statement->args[i].value.ival),
                                       SQLT_INT,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_USHORT:
            statement->args[i].value.uval = *(unsigned short*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.uval,
                                       sizeof(statement->args[i].value.uval),
                                       SQLT_UIN,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_INT:
            statement->args[i].value.ival = *(int*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.ival,
                                       sizeof(statement->args[i].value.ival),
                                       SQLT_INT,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_UINT:
            statement->args[i].value.uval = *(unsigned int*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.uval,
                                       sizeof(statement->args[i].value.uval),
                                       SQLT_UIN,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_LONG:
            statement->args[i].value.sval =
                apr_psprintf(statement->pool, "%ld", *(long*)values[j]);
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       statement->args[i].value.sval,
                                       strlen(statement->args[i].value.sval)+1,
                                       SQLT_STR,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_ULONG:
            statement->args[i].value.sval =
                apr_psprintf(statement->pool, "%lu",
                                              *(unsigned long*)values[j]);
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       statement->args[i].value.sval,
                                       strlen(statement->args[i].value.sval)+1,
                                       SQLT_STR,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_LONGLONG:
            statement->args[i].value.sval =
                apr_psprintf(statement->pool, "%" APR_INT64_T_FMT,
                                              *(apr_int64_t*)values[j]);
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       statement->args[i].value.sval,
                                       strlen(statement->args[i].value.sval)+1,
                                       SQLT_STR,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_ULONGLONG:
            statement->args[i].value.sval =
                apr_psprintf(statement->pool, "%" APR_UINT64_T_FMT,
                                              *(apr_uint64_t*)values[j]);
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       statement->args[i].value.sval,
                                       strlen(statement->args[i].value.sval)+1,
                                       SQLT_UIN,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_FLOAT:
            statement->args[i].value.fval = *(float*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.fval,
                                       sizeof(statement->args[i].value.fval),
                                       SQLT_FLT,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_DOUBLE:
            statement->args[i].value.fval = *(double*)values[j];
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       &statement->args[i].value.fval,
                                       sizeof(statement->args[i].value.fval),
                                       SQLT_FLT,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_STRING:
        case APR_DBD_TYPE_TEXT:
        case APR_DBD_TYPE_TIME:
        case APR_DBD_TYPE_DATE:
        case APR_DBD_TYPE_DATETIME:
        case APR_DBD_TYPE_TIMESTAMP:
        case APR_DBD_TYPE_ZTIMESTAMP:
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       (dvoid*) values[j],
                                       strlen(values[j]) + 1,
                                       SQLT_STR,
                                       &statement->args[i].ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        case APR_DBD_TYPE_BLOB:
            {
            char *data = (char *)values[j];
            apr_size_t size = *(apr_size_t*)values[++j];

            /* skip table and column for now */
            j += 2;

            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       data, size, SQLT_LBI,
                                       &statement->args[i].ind,
                                       NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            }
            break;
        case APR_DBD_TYPE_CLOB:
            {
            char *data = (char *)values[j];
            apr_size_t size = *(apr_size_t*)values[++j];

            /* skip table and column for now */
            j += 2;

            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       data, size, SQLT_LNG,
                                       &statement->args[i].ind,
                                       NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            }
            break;
        case APR_DBD_TYPE_NULL:
        default:
            sql->status = OCIBindByPos(stmt, &statement->args[i].bind,
                                       sql->err, i + 1,
                                       NULL, 0, SQLT_STR,
                                       &null_ind, NULL,
                                       (ub2) 0, (ub4) 0,
                                       (ub4 *) 0, OCI_DEFAULT);
            break;
        }

        if (sql->status != OCI_SUCCESS) {
            return;
        }
    }

    return;
}

static int dbd_oracle_pbquery(apr_pool_t * pool, apr_dbd_t * sql,
                              int *nrows, apr_dbd_prepared_t * statement,
                              const void **values)
{
    OCISnapshot *oldsnapshot = NULL;
    OCISnapshot *newsnapshot = NULL;
    apr_dbd_transaction_t* trans = sql->trans;
    int exec_mode;
    int_errorcode;

    if (trans) {
        switch (trans->status) {
        case TRANS_ERROR:
            return -1;
        case TRANS_NONE:
            trans = NULL;
            break;
        case TRANS_1:
            oldsnapshot = trans->snapshot1;
            newsnapshot = trans->snapshot2;
            trans->status = TRANS_2;
            break;
        case TRANS_2:
            oldsnapshot = trans->snapshot2;
            newsnapshot = trans->snapshot1;
            trans->status = TRANS_1;
            break;
        }
        exec_mode = OCI_DEFAULT;
    }
    else {
        exec_mode = OCI_COMMIT_ON_SUCCESS;
    }

    dbd_oracle_bbind(statement, values);

    sql->status = OCIStmtExecute(sql->svc, statement->stmt, sql->err, 1, 0,
                                 oldsnapshot, newsnapshot, exec_mode);
    switch (sql->status) {
    case OCI_SUCCESS:
        break;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Execute error %d: %s\n", sql->status, sql->buf);
#endif
        /* fallthrough */
    default:
        if (TXN_NOTICE_ERRORS(trans)) {
            trans->status = TRANS_ERROR;
        }
        return 1;
    }

    sql->status = OCIAttrGet(statement->stmt, OCI_HTYPE_STMT, nrows, 0,
                             OCI_ATTR_ROW_COUNT, sql->err);
    return 0;
}

static int dbd_oracle_pvbquery(apr_pool_t * pool, apr_dbd_t * sql,
                               int *nrows, apr_dbd_prepared_t * statement,
                               va_list args)
{
    const void **values;
    int i;

    if (sql->trans && sql->trans->status == TRANS_ERROR) {
        return -1;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const void*);
    }

    return dbd_oracle_pbquery(pool, sql, nrows, statement, values);
}

static int dbd_oracle_pbselect(apr_pool_t * pool, apr_dbd_t * sql,
                               apr_dbd_results_t ** results,
                               apr_dbd_prepared_t * statement,
                               int seek, const void **values)
{
    int exec_mode = seek ? OCI_STMT_SCROLLABLE_READONLY : OCI_DEFAULT;
    OCISnapshot *oldsnapshot = NULL;
    OCISnapshot *newsnapshot = NULL;
    apr_dbd_transaction_t* trans = sql->trans;
    int_errorcode;

    if (trans) {
        switch (trans->status) {
        case TRANS_ERROR:
            return 1;
        case TRANS_NONE:
            trans = NULL;
            break;
        case TRANS_1:
            oldsnapshot = trans->snapshot1;
            newsnapshot = trans->snapshot2;
            trans->status = TRANS_2;
            break;
        case TRANS_2:
            oldsnapshot = trans->snapshot2;
            newsnapshot = trans->snapshot1;
            trans->status = TRANS_1;
            break;
        }
    }

    dbd_oracle_bbind(statement, values);

    sql->status = OCIStmtExecute(sql->svc, statement->stmt, sql->err, 0, 0,
                                 oldsnapshot, newsnapshot, exec_mode);
    switch (sql->status) {
    case OCI_SUCCESS:
        break;
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode,
                    sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Executing prepared statement: %s\n", sql->buf);
#endif
        /* fallthrough */
    default:
        if (TXN_NOTICE_ERRORS(trans)) {
            trans->status = TRANS_ERROR;
        }
        return 1;
    }

    if (!*results) {
        *results = apr_palloc(pool, sizeof(apr_dbd_results_t));
    }
    (*results)->handle = sql;
    (*results)->statement = statement;
    (*results)->seek = seek;
    (*results)->rownum = seek ? 0 : -1;
    (*results)->pool = pool;

    return 0;
}

static int dbd_oracle_pvbselect(apr_pool_t * pool, apr_dbd_t * sql,
                                apr_dbd_results_t ** results,
                                apr_dbd_prepared_t * statement, int seek,
                                va_list args)
{
    const void **values;
    int i;

    if (sql->trans && sql->trans->status == TRANS_ERROR) {
        return -1;
    }

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);

    for (i = 0; i < statement->nvals; i++) {
        values[i] = va_arg(args, const void*);
    }

    return dbd_oracle_pbselect(pool, sql, results, statement, seek, values);
}

static int dbd_oracle_start_transaction(apr_pool_t *pool, apr_dbd_t *sql,
                                        apr_dbd_transaction_t **trans)
{
    int ret = 0;
    int_errorcode;
    if (*trans) {
        dbd_oracle_end_transaction(*trans);
    }
    else {
        *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
        OCIHandleAlloc(dbd_oracle_env, (dvoid**)&(*trans)->trans,
                       OCI_HTYPE_TRANS, 0, 0);
        OCIAttrSet(sql->svc, OCI_HTYPE_SVCCTX, (*trans)->trans, 0,
                   OCI_ATTR_TRANS, sql->err);
    }


    sql->status = OCITransStart(sql->svc, sql->err, TRANS_TIMEOUT,
                                OCI_TRANS_NEW);
    switch (sql->status) {
    case OCI_ERROR:
#ifdef DEBUG
        OCIErrorGet(sql->err, 1, NULL, &errorcode, sql->buf,
                    sizeof(sql->buf), OCI_HTYPE_ERROR);
        printf("Transaction: %s\n", sql->buf);
#endif
        ret = 1;
        break;
    case OCI_SUCCESS:
        (*trans)->handle = sql;
        (*trans)->status = TRANS_1;
        sql->trans = *trans;
        switch (OCIDescriptorAlloc(dbd_oracle_env,
                                   (dvoid**)&(*trans)->snapshot1,
                                   OCI_DTYPE_SNAP, 0, NULL)) {
        case OCI_SUCCESS:
            apr_pool_cleanup_register(pool, (*trans)->snapshot1,
                                      dbd_free_snapshot, apr_pool_cleanup_null);
            break;
        case OCI_INVALID_HANDLE:
            ret = 1;
            break;
        }
        switch (OCIDescriptorAlloc(dbd_oracle_env,
                                   (dvoid**)&(*trans)->snapshot2,
                                   OCI_DTYPE_SNAP, 0, NULL)) {
        case OCI_SUCCESS:
            apr_pool_cleanup_register(pool, (*trans)->snapshot2,
                                      dbd_free_snapshot, apr_pool_cleanup_null);
            break;
        case OCI_INVALID_HANDLE:
            ret = 1;
            break;
        }
        break;
    default:
        ret = 1;
        break;
    }
    return ret;
}

static int dbd_oracle_end_transaction(apr_dbd_transaction_t *trans)
{
    int ret = 1;             /* no transaction is an error cond */
    sword status;
    apr_dbd_t *handle = trans->handle;
    if (trans) {
        switch (trans->status) {
        case TRANS_NONE:     /* No trans is an error here */
            status = OCI_ERROR;
            break;
        case TRANS_ERROR:
            status = OCITransRollback(handle->svc, handle->err, OCI_DEFAULT);
            break;
        default:
            /* rollback on explicit rollback request */
            if (TXN_DO_ROLLBACK(trans)) {
                status = OCITransRollback(handle->svc, handle->err, OCI_DEFAULT);
            } else {
                status = OCITransCommit(handle->svc, handle->err, OCI_DEFAULT);
            }
            break;
        }

        handle->trans = NULL;

        switch (status) {
        case OCI_SUCCESS:
            ret = 0;
            break;
        default:
            ret = 3;
            break;
        }
    }
    return ret;
}

static int dbd_oracle_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode;
}

static int dbd_oracle_transaction_mode_set(apr_dbd_transaction_t *trans,
                                           int mode)
{
    if (!trans)
        return APR_DBD_TRANSACTION_COMMIT;

    return trans->mode = (mode & TXN_MODE_BITS);
}

/* This doesn't work for BLOB because of NULLs, but it can fake it
 * if the BLOB is really a string
 */
static const char *dbd_oracle_get_entry(const apr_dbd_row_t *row, int n)
{
    ub4 len = 0;
    ub1 csform = 0;
    ub2 csid = 0;
    apr_size_t buflen = 0;
    char *buf = NULL;
    define_arg *val = &row->res->statement->out[n];
    apr_dbd_t *sql = row->res->handle;
    int_errorcode;

    if ((n < 0) || (n >= row->res->statement->nout) || (val->ind == -1)) {
        return NULL;
    }

    switch (val->type) {
    case SQLT_BLOB:
    case SQLT_CLOB:
        sql->status = OCILobGetLength(sql->svc, sql->err, val->buf.lobval,
                                      &len);
        switch (sql->status) {
        case OCI_SUCCESS:
        case OCI_SUCCESS_WITH_INFO:
            if (len == 0) {
                buf = "";
            }
            break;
        case OCI_ERROR:
#ifdef DEBUG
            OCIErrorGet(sql->err, 1, NULL, &errorcode,
                        sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
            printf("Finding LOB length: %s\n", sql->buf);
            break;
#endif
        default:
            break;
        }

        if (len == 0) {
            break;
        }

        if (val->type == APR_DBD_TYPE_CLOB) {
#if 1
            /* Is this necessary, or can it be defaulted? */
            sql->status = OCILobCharSetForm(dbd_oracle_env, sql->err,
                                            val->buf.lobval, &csform);
            if (sql->status == OCI_SUCCESS) {
                sql->status = OCILobCharSetId(dbd_oracle_env, sql->err,
                                              val->buf.lobval, &csid);
            }
            switch (sql->status) {
            case OCI_SUCCESS:
            case OCI_SUCCESS_WITH_INFO:
                buflen = (len+1) * 4; /* ugh, wasteful UCS-4 handling */
                /* zeroise all - where the string ends depends on charset */
                buf = apr_pcalloc(row->pool, buflen);
                break;
#ifdef DEBUG
            case OCI_ERROR:
                OCIErrorGet(sql->err, 1, NULL, &errorcode,
                            sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
                printf("Reading LOB character set: %s\n", sql->buf);
                break; /*** XXX?? ***/
#endif
            default:
                break; /*** XXX?? ***/
            }
#else   /* ignore charset */
            buflen = (len+1) * 4; /* ugh, wasteful UCS-4 handling */
            /* zeroise all - where the string ends depends on charset */
            buf = apr_pcalloc(row->pool, buflen);
#endif
        } else {
            /* BUG: this'll only work if the BLOB looks like a string */
            buflen = len;
            buf = apr_palloc(row->pool, buflen+1);
            buf[buflen] = 0;
        }

        if (!buf) {
            break;
        }

        sql->status = OCILobRead(sql->svc, sql->err, val->buf.lobval,
                                 &len, 1, (dvoid*) buf, buflen,
                                 NULL, NULL, csid, csform);
        switch (sql->status) {
        case OCI_SUCCESS:
        case OCI_SUCCESS_WITH_INFO:
            break;
#ifdef DEBUG
        case OCI_ERROR:
            OCIErrorGet(sql->err, 1, NULL, &errorcode,
                        sql->buf, sizeof(sql->buf), OCI_HTYPE_ERROR);
            printf("Reading LOB: %s\n", sql->buf);
            buf = NULL; /*** XXX?? ***/
            break;
#endif
        default:
            buf = NULL; /*** XXX?? ***/
            break;
        }

        break;
    case SQLT_LNG:
    case SQLT_LBI:
        /* raw is struct { ub4 len; char *buf; } */
        len = *(ub4*) val->buf.raw;
        buf = apr_pstrndup(row->pool, val->buf.sval + sizeof(ub4), len);
        break;
    default:
        buf = apr_pstrndup(row->pool, val->buf.sval, val->len);
        break;
    }
    return (const char*) buf;
}

/* XXX Should this use Oracle proper API instead of calling get_entry()? */
static apr_status_t dbd_oracle_datum_get(const apr_dbd_row_t *row, int n,
                                         apr_dbd_type_e type, void *data)
{
    define_arg *val = &row->res->statement->out[n];
    const char *entry;

    if ((n < 0) || (n >= row->res->statement->nout)) {
        return APR_EGENERAL;
    }

    if(val->ind == -1) {
        return APR_ENOENT;
    }

    switch (type) {
    case APR_DBD_TYPE_TINY:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(char*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_UTINY:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(unsigned char*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_SHORT:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(short*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_USHORT:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(unsigned short*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_INT:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(int*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_UINT:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(unsigned int*)data = atoi(entry);
        break;
    case APR_DBD_TYPE_LONG:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(long*)data = atol(entry);
        break;
    case APR_DBD_TYPE_ULONG:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(unsigned long*)data = atol(entry);
        break;
    case APR_DBD_TYPE_LONGLONG:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(apr_int64_t*)data = apr_atoi64(entry);
        break;
    case APR_DBD_TYPE_ULONGLONG:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(apr_uint64_t*)data = apr_atoi64(entry);
        break;
    case APR_DBD_TYPE_FLOAT:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(float*)data = (float)atof(entry);
        break;
    case APR_DBD_TYPE_DOUBLE:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(double*)data = atof(entry);
        break;
    case APR_DBD_TYPE_STRING:
    case APR_DBD_TYPE_TEXT:
    case APR_DBD_TYPE_TIME:
    case APR_DBD_TYPE_DATE:
    case APR_DBD_TYPE_DATETIME:
    case APR_DBD_TYPE_TIMESTAMP:
    case APR_DBD_TYPE_ZTIMESTAMP:
        entry = dbd_oracle_get_entry(row, n);
        if (entry == NULL) {
            return APR_ENOENT;
        }
        *(char**)data = (char*)entry;
        break;
    case APR_DBD_TYPE_BLOB:
    case APR_DBD_TYPE_CLOB:
        {
        apr_bucket *e;
        apr_bucket_brigade *b = (apr_bucket_brigade*)data;
        apr_dbd_t *sql = row->res->handle;
        ub4 len = 0;

        switch (val->type) {
        case SQLT_BLOB:
        case SQLT_CLOB:
            sql->status = OCILobGetLength(sql->svc, sql->err,
                                          val->buf.lobval, &len);
            switch(sql->status) {
            case OCI_SUCCESS:
            case OCI_SUCCESS_WITH_INFO:
                if (len == 0) {
                    e = apr_bucket_eos_create(b->bucket_alloc);
                }
                else {
                    e = apr_bucket_lob_create(row, n, 0, len,
                                              row->pool, b->bucket_alloc);
                }
                break;
            default:
                return APR_ENOENT;
            }
            break;
        default:
            entry = dbd_oracle_get_entry(row, n);
            if (entry == NULL) {
                return APR_ENOENT;
            }
            e = apr_bucket_pool_create(entry, strlen(entry),
                                       row->pool, b->bucket_alloc);
            break;
        }
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

static apr_status_t dbd_oracle_close(apr_dbd_t *handle)
{
    /* FIXME: none of the oracle docs/examples say anything about
     * closing/releasing handles.  Which seems unlikely ...
     */

    /* OK, let's grab from cdemo again.
     * cdemo81 does nothing; cdemo82 does OCIHandleFree on the handles
     */
    switch (OCISessionEnd(handle->svc, handle->err, handle->auth,
            (ub4)OCI_DEFAULT)) {
    default:
        break;
    }
    switch (OCIServerDetach(handle->svr, handle->err, (ub4) OCI_DEFAULT )) {
    default:
        break;
    }
    /* does OCISessionEnd imply this? */
    switch (OCIHandleFree((dvoid *) handle->auth, (ub4) OCI_HTYPE_SESSION)) {
    default:
        break;
    }
    switch (OCIHandleFree((dvoid *) handle->svr, (ub4) OCI_HTYPE_SERVER)) {
    default:
        break;
    }
    switch (OCIHandleFree((dvoid *) handle->svc, (ub4) OCI_HTYPE_SVCCTX)) {
    default:
        break;
    }
    switch (OCIHandleFree((dvoid *) handle->err, (ub4) OCI_HTYPE_ERROR)) {
    default:
        break;
    }
    return APR_SUCCESS;
}

static apr_status_t dbd_oracle_check_conn(apr_pool_t *pool, apr_dbd_t *sql)
{
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    
    if(dbd_oracle_pselect(pool, sql, &res, sql->check_conn_stmt,
                          0, NULL) != 0) {
        return APR_EGENERAL;
    }
    
    if(dbd_oracle_get_row(pool, res, &row, -1) != 0) {
        return APR_EGENERAL;
    }
    
    if(dbd_oracle_get_row(pool, res, &row, -1) != -1) {
        return APR_EGENERAL;
    }

    return APR_SUCCESS;
}

static int dbd_oracle_select_db(apr_pool_t *pool, apr_dbd_t *handle,
                                const char *name)
{
    /* FIXME: need to find this in the docs */
    return APR_ENOTIMPL;
}

static void *dbd_oracle_native(apr_dbd_t *handle)
{
    /* FIXME: can we do anything better?  Oracle doesn't seem to have
     * a concept of a handle in the sense we use it.
     */
    return dbd_oracle_env;
}

static int dbd_oracle_num_cols(apr_dbd_results_t* res)
{
    return res->statement->nout;
}

static int dbd_oracle_num_tuples(apr_dbd_results_t* res)
{
    if (!res->seek) {
        return -1;
    }
    if (res->nrows >= 0) {
        return res->nrows;
    }
    res->handle->status = OCIAttrGet(res->statement->stmt, OCI_HTYPE_STMT,
                                     &res->nrows, 0, OCI_ATTR_ROW_COUNT,
                                     res->handle->err);
    return res->nrows;
}

APU_MODULE_DECLARE_DATA const apr_dbd_driver_t apr_dbd_oracle_driver = {
    "oracle",
    dbd_oracle_init,
    dbd_oracle_native,
    dbd_oracle_open,
    dbd_oracle_check_conn,
    dbd_oracle_close,
    dbd_oracle_select_db,
    dbd_oracle_start_transaction,
    dbd_oracle_end_transaction,
    dbd_oracle_query,
    dbd_oracle_select,
    dbd_oracle_num_cols,
    dbd_oracle_num_tuples,
    dbd_oracle_get_row,
    dbd_oracle_get_entry,
    dbd_oracle_error,
    dbd_oracle_escape,
    dbd_oracle_prepare,
    dbd_oracle_pvquery,
    dbd_oracle_pvselect,
    dbd_oracle_pquery,
    dbd_oracle_pselect,
    dbd_oracle_get_name,
    dbd_oracle_transaction_mode_get,
    dbd_oracle_transaction_mode_set,
    ":apr%d",
    dbd_oracle_pvbquery,
    dbd_oracle_pvbselect,
    dbd_oracle_pbquery,
    dbd_oracle_pbselect,
    dbd_oracle_datum_get
};
#endif
