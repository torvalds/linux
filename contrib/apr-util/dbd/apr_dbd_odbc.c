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
#if APU_HAVE_ODBC

#include "apr.h"
#include "apr_strings.h"
#include "apr_buckets.h"
#include "apr_env.h"
#include "apr_file_io.h"
#include "apr_file_info.h"
#include "apr_dbd_internal.h"
#include "apr_thread_proc.h"
#include "apu_version.h"
#include "apu_config.h"

#include <stdlib.h>

/* If library is ODBC-V2, use macros for limited ODBC-V2 support 
 * No random access in V2.
 */
#ifdef ODBCV2
#define ODBCVER 0x0200
#include "apr_dbd_odbc_v2.h"
#endif

/* standard ODBC include files */
#ifdef HAVE_SQL_H
#include <sql.h>
#include <sqlext.h>
#elif defined(HAVE_ODBC_SQL_H)
#include <odbc/sql.h>
#include <odbc/sqlext.h>
#endif

/*
* MSVC6 does not support intptr_t (C99)
* APR does not have a signed inptr type until 2.0  (r1557720)
*/
#if defined(_MSC_VER) && _MSC_VER < 1400
#if APR_SIZEOF_VOIDP == 8
#define   ODBC_INTPTR_T  apr_int64_t
#else
#define   ODBC_INTPTR_T  apr_int32_t
#endif
#else
#define   ODBC_INTPTR_T  intptr_t
#endif


/* Driver name is "odbc" and the entry point is 'apr_dbd_odbc_driver' 
 * unless ODBC_DRIVER_NAME is defined and it is linked with another db library which
 * is ODBC source-compatible. e.g. DB2, Informix, TimesTen, mysql.  
 */
#ifndef ODBC_DRIVER_NAME
#define ODBC_DRIVER_NAME odbc
#endif
#define STRINGIFY(x) #x
#define NAMIFY2(n) apr_dbd_##n##_driver
#define NAMIFY1(n) NAMIFY2(n)
#define ODBC_DRIVER_STRING STRINGIFY(ODBC_DRIVER_NAME)
#define ODBC_DRIVER_ENTRY NAMIFY1(ODBC_DRIVER_NAME)

/* Required APR version for this driver */
#define DRIVER_APU_VERSION_MAJOR APU_MAJOR_VERSION
#define DRIVER_APU_VERSION_MINOR APU_MINOR_VERSION

static SQLHANDLE henv = NULL;           /* ODBC ENV handle is process-wide */

/* Use a CHECK_ERROR macro so we can grab the source line numbers
 * for error reports
 */
static void check_error(apr_dbd_t *a, const char *step, SQLRETURN rc,
                 SQLSMALLINT type, SQLHANDLE h, int line);
#define CHECK_ERROR(a,s,r,t,h)  check_error(a,s,r,t,h, __LINE__)

#define SOURCE_FILE __FILE__            /* source file for error messages */
#define MAX_ERROR_STRING 1024           /* max length of message in dbc */
#define MAX_COLUMN_NAME 256             /* longest column name recognized */
#define DEFAULT_BUFFER_SIZE 1024        /* value for defaultBufferSize */

#define MAX_PARAMS  20
#define DEFAULTSEPS " \t\r\n,="
#define CSINGLEQUOTE '\''
#define SSINGLEQUOTE "\'"

#define TEXTMODE 1              /* used for text (APR 1.2) mode params */
#define BINARYMODE 0            /* used for binary (APR 1.3+) mode params */

/* Identify datatypes which are LOBs 
 * - DB2 DRDA driver uses undefined types -98 and -99 for CLOB & BLOB
 */
#define IS_LOB(t)  (t == SQL_LONGVARCHAR \
     || t == SQL_LONGVARBINARY || t == SQL_VARBINARY \
     || t == -98 || t == -99)

/* These types are CLOBs 
 * - DB2 DRDA driver uses undefined type -98 for CLOB
 */
#define IS_CLOB(t) \
    (t == SQL_LONGVARCHAR || t == -98)

/* Convert a SQL result to an APR result */
#define APR_FROM_SQL_RESULT(rc) \
    (SQL_SUCCEEDED(rc) ? APR_SUCCESS : APR_EGENERAL)

/* DBD opaque structures */
struct apr_dbd_t
{
    SQLHANDLE dbc;              /* SQL connection handle - NULL after close */
    apr_pool_t *pool;           /* connection lifetime pool */
    char *dbname;               /* ODBC datasource */
    int lasterrorcode;
    int lineNumber;
    char lastError[MAX_ERROR_STRING];
    int defaultBufferSize;      /* used for CLOBs in text mode, 
                                 * and when fld size is indeterminate */
    ODBC_INTPTR_T transaction_mode;
    ODBC_INTPTR_T dboptions;         /* driver options re SQLGetData */
    ODBC_INTPTR_T default_transaction_mode;
    int can_commit;             /* controls end_trans behavior */
};

struct apr_dbd_results_t
{
    SQLHANDLE stmt;             /* parent sql statement handle */
    SQLHANDLE dbc;              /* parent sql connection handle */
    apr_pool_t *pool;           /* pool from query or select */
    apr_dbd_t *apr_dbd;         /* parent DBD connection handle */
    int random;                 /* random access requested */
    int ncols;                  /* number of columns */
    int isclosed;               /* cursor has been closed */
    char **colnames;            /* array of column names (NULL until used) */
    SQLPOINTER *colptrs;        /* pointers to column data */
    SQLINTEGER *colsizes;       /* sizes for columns (enough for txt or bin) */
    SQLINTEGER *coltextsizes;   /* max-sizes if converted to text */
    SQLSMALLINT *coltypes;      /* array of SQL data types for columns */
    SQLLEN *colinds;            /* array of SQL data indicator/strlens */
    int *colstate;              /* array of column states
                                 * - avail, bound, present, unavail 
                                 */
    int *all_data_fetched;      /* flags data as all fetched, for LOBs  */
    void *data;                 /* buffer for all data for one row */
};

enum                            /* results column states */
{
    COL_AVAIL,                  /* data may be retrieved with SQLGetData */
    COL_PRESENT,                /* data has been retrieved with SQLGetData */
    COL_BOUND,                  /* column is bound to colptr */
    COL_RETRIEVED,              /* all data from column has been returned */
    COL_UNAVAIL                 /* column is unavailable because ODBC driver
                                 *  requires that columns be retrieved
                                 *  in ascending order and a higher col 
                                 *  was accessed
                                 */
};

struct apr_dbd_row_t {
    SQLHANDLE stmt;             /* parent ODBC statement handle */
    SQLHANDLE dbc;              /* parent ODBC connection handle */
    apr_pool_t *pool;           /* pool from get_row */
    apr_dbd_results_t *res;
};

struct apr_dbd_transaction_t {
    SQLHANDLE dbc;              /* parent ODBC connection handle */
    apr_dbd_t *apr_dbd;         /* parent DBD connection handle */
};

struct apr_dbd_prepared_t {
    SQLHANDLE stmt;             /* ODBC statement handle */
    SQLHANDLE dbc;              /* parent ODBC connection handle */
    apr_dbd_t *apr_dbd;
    int nargs;
    int nvals;
    int *types;                 /* array of DBD data types */
};

static void odbc_lob_bucket_destroy(void *data);
static apr_status_t odbc_lob_bucket_setaside(apr_bucket *e, apr_pool_t *pool);
static apr_status_t odbc_lob_bucket_read(apr_bucket *e, const char **str,
                                         apr_size_t *len, apr_read_type_e block);

/* the ODBC LOB bucket type */
static const apr_bucket_type_t odbc_bucket_type = {
    "ODBC_LOB", 5, APR_BUCKET_DATA,
    odbc_lob_bucket_destroy,
    odbc_lob_bucket_read,
    odbc_lob_bucket_setaside,
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};

/* ODBC LOB bucket data */
typedef struct {
    /** Ref count for shared bucket */
    apr_bucket_refcount  refcount;
    const apr_dbd_row_t *row;
    int col;
    SQLSMALLINT type;
} odbc_bucket;

/* SQL datatype mappings to DBD datatypes 
 * These tables must correspond *exactly* to the apr_dbd_type_e enum 
 * in apr_dbd.h
 */

/* ODBC "C" types to DBD datatypes  */
static SQLSMALLINT const sqlCtype[] = {
    SQL_C_DEFAULT,                  /* APR_DBD_TYPE_NONE              */
    SQL_C_STINYINT,                 /* APR_DBD_TYPE_TINY,       \%hhd */
    SQL_C_UTINYINT,                 /* APR_DBD_TYPE_UTINY,      \%hhu */
    SQL_C_SSHORT,                   /* APR_DBD_TYPE_SHORT,      \%hd  */
    SQL_C_USHORT,                   /* APR_DBD_TYPE_USHORT,     \%hu  */
    SQL_C_SLONG,                    /* APR_DBD_TYPE_INT,        \%d   */
    SQL_C_ULONG,                    /* APR_DBD_TYPE_UINT,       \%u   */
    SQL_C_SLONG,                    /* APR_DBD_TYPE_LONG,       \%ld  */
    SQL_C_ULONG,                    /* APR_DBD_TYPE_ULONG,      \%lu  */
    SQL_C_SBIGINT,                  /* APR_DBD_TYPE_LONGLONG,   \%lld */
    SQL_C_UBIGINT,                  /* APR_DBD_TYPE_ULONGLONG,  \%llu */
    SQL_C_FLOAT,                    /* APR_DBD_TYPE_FLOAT,      \%f   */
    SQL_C_DOUBLE,                   /* APR_DBD_TYPE_DOUBLE,     \%lf  */
    SQL_C_CHAR,                     /* APR_DBD_TYPE_STRING,     \%s   */
    SQL_C_CHAR,                     /* APR_DBD_TYPE_TEXT,       \%pDt */
    SQL_C_CHAR, /*SQL_C_TYPE_TIME,      APR_DBD_TYPE_TIME,       \%pDi */
    SQL_C_CHAR, /*SQL_C_TYPE_DATE,      APR_DBD_TYPE_DATE,       \%pDd */
    SQL_C_CHAR, /*SQL_C_TYPE_TIMESTAMP, APR_DBD_TYPE_DATETIME,   \%pDa */
    SQL_C_CHAR, /*SQL_C_TYPE_TIMESTAMP, APR_DBD_TYPE_TIMESTAMP,  \%pDs */
    SQL_C_CHAR, /*SQL_C_TYPE_TIMESTAMP, APR_DBD_TYPE_ZTIMESTAMP, \%pDz */
    SQL_LONGVARBINARY,              /* APR_DBD_TYPE_BLOB,       \%pDb */
    SQL_LONGVARCHAR,                /* APR_DBD_TYPE_CLOB,       \%pDc */
    SQL_TYPE_NULL                   /* APR_DBD_TYPE_NULL        \%pDn */
};
#define NUM_APR_DBD_TYPES (sizeof(sqlCtype) / sizeof(sqlCtype[0]))

/*  ODBC Base types to DBD datatypes */
static SQLSMALLINT const sqlBaseType[] = {
    SQL_C_DEFAULT,              /* APR_DBD_TYPE_NONE              */
    SQL_TINYINT,                /* APR_DBD_TYPE_TINY,       \%hhd */
    SQL_TINYINT,                /* APR_DBD_TYPE_UTINY,      \%hhu */
    SQL_SMALLINT,               /* APR_DBD_TYPE_SHORT,      \%hd  */
    SQL_SMALLINT,               /* APR_DBD_TYPE_USHORT,     \%hu  */
    SQL_INTEGER,                /* APR_DBD_TYPE_INT,        \%d   */
    SQL_INTEGER,                /* APR_DBD_TYPE_UINT,       \%u   */
    SQL_INTEGER,                /* APR_DBD_TYPE_LONG,       \%ld  */
    SQL_INTEGER,                /* APR_DBD_TYPE_ULONG,      \%lu  */
    SQL_BIGINT,                 /* APR_DBD_TYPE_LONGLONG,   \%lld */
    SQL_BIGINT,                 /* APR_DBD_TYPE_ULONGLONG,  \%llu */
    SQL_FLOAT,                  /* APR_DBD_TYPE_FLOAT,      \%f   */
    SQL_DOUBLE,                 /* APR_DBD_TYPE_DOUBLE,     \%lf  */
    SQL_CHAR,                   /* APR_DBD_TYPE_STRING,     \%s   */
    SQL_CHAR,                   /* APR_DBD_TYPE_TEXT,       \%pDt */
    SQL_CHAR, /*SQL_TIME,          APR_DBD_TYPE_TIME,       \%pDi */
    SQL_CHAR, /*SQL_DATE,          APR_DBD_TYPE_DATE,       \%pDd */
    SQL_CHAR, /*SQL_TIMESTAMP,     APR_DBD_TYPE_DATETIME,   \%pDa */
    SQL_CHAR, /*SQL_TIMESTAMP,     APR_DBD_TYPE_TIMESTAMP,  \%pDs */
    SQL_CHAR, /*SQL_TIMESTAMP,     APR_DBD_TYPE_ZTIMESTAMP, \%pDz */
    SQL_LONGVARBINARY,          /* APR_DBD_TYPE_BLOB,       \%pDb */
    SQL_LONGVARCHAR,            /* APR_DBD_TYPE_CLOB,       \%pDc */
    SQL_TYPE_NULL               /* APR_DBD_TYPE_NULL        \%pDn */
};

/*  result sizes for DBD datatypes (-1 for null-terminated) */
static int const sqlSizes[] = {
    0,
    sizeof(char),               /**< \%hhd out: char* */
    sizeof(unsigned char),      /**< \%hhu out: unsigned char* */
    sizeof(short),              /**< \%hd  out: short* */
    sizeof(unsigned short),     /**< \%hu  out: unsigned short* */
    sizeof(int),                /**< \%d   out: int* */
    sizeof(unsigned int),       /**< \%u   out: unsigned int* */
    sizeof(long),               /**< \%ld  out: long* */
    sizeof(unsigned long),      /**< \%lu  out: unsigned long* */
    sizeof(apr_int64_t),        /**< \%lld out: apr_int64_t* */
    sizeof(apr_uint64_t),       /**< \%llu out: apr_uint64_t* */
    sizeof(float),              /**< \%f   out: float* */
    sizeof(double),             /**< \%lf  out: double* */
    -1,                         /**< \%s   out: char** */
    -1,                         /**< \%pDt out: char** */
    -1,                         /**< \%pDi out: char** */
    -1,                         /**< \%pDd out: char** */
    -1,                         /**< \%pDa out: char** */
    -1,                         /**< \%pDs out: char** */
    -1,                         /**< \%pDz out: char** */
    sizeof(apr_bucket_brigade), /**< \%pDb out: apr_bucket_brigade* */
    sizeof(apr_bucket_brigade), /**< \%pDc out: apr_bucket_brigade* */
    0                           /**< \%pDn : in: void*, out: void** */
};

/*
 * local functions
 */

/* close any open results for the connection */
static apr_status_t odbc_close_results(void *d)
{
    apr_dbd_results_t *dbr = (apr_dbd_results_t *)d;
    SQLRETURN rc = SQL_SUCCESS;
    
    if (dbr && dbr->apr_dbd && dbr->apr_dbd->dbc) {
    	if (!dbr->isclosed)
            rc = SQLCloseCursor(dbr->stmt);
    	dbr->isclosed = 1;
    }
    return APR_FROM_SQL_RESULT(rc);
}

/* close the ODBC statement handle from a  prepare */
static apr_status_t odbc_close_pstmt(void *s)
{   
    SQLRETURN rc = APR_SUCCESS;
    apr_dbd_prepared_t *statement = s;

    /* stmt is closed if connection has already been closed */
    if (statement) {
        SQLHANDLE hstmt = statement->stmt;

        if (hstmt && statement->apr_dbd && statement->apr_dbd->dbc) {
            rc = SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }
        statement->stmt = NULL;
    }
    return APR_FROM_SQL_RESULT(rc);
}

/* close: close/release a connection obtained from open() */
static apr_status_t odbc_close(apr_dbd_t *handle)
{
    SQLRETURN rc = SQL_SUCCESS;

    if (handle->dbc) {
        rc = SQLDisconnect(handle->dbc);
        CHECK_ERROR(handle, "SQLDisconnect", rc, SQL_HANDLE_DBC, handle->dbc);
        rc = SQLFreeHandle(SQL_HANDLE_DBC, handle->dbc);
        CHECK_ERROR(handle, "SQLFreeHandle (DBC)", rc, SQL_HANDLE_ENV, henv);
        handle->dbc = NULL;
    }
    return APR_FROM_SQL_RESULT(rc);
}

/* odbc_close re-defined for passing to pool cleanup */
static apr_status_t odbc_close_cleanup(void *handle)
{
    return odbc_close((apr_dbd_t *)handle);
}

/* close the ODBC environment handle at process termination */
static apr_status_t odbc_close_env(SQLHANDLE henv)
{   
    SQLRETURN rc;

    rc = SQLFreeHandle(SQL_HANDLE_ENV, henv);
    henv = NULL;
    return APR_FROM_SQL_RESULT(rc);
}

/* setup the arrays in results for all the returned columns */
static SQLRETURN odbc_set_result_column(int icol, apr_dbd_results_t *res, 
                                        SQLHANDLE stmt)
{
    SQLRETURN rc;
    ODBC_INTPTR_T maxsize, textsize, realsize, type, isunsigned = 1;

    /* discover the sql type */
    rc = SQLColAttribute(stmt, icol + 1, SQL_DESC_UNSIGNED, NULL, 0, NULL,
                         (SQLPOINTER)&isunsigned);
    isunsigned = (isunsigned == SQL_TRUE);

    rc = SQLColAttribute(stmt, icol + 1, SQL_DESC_TYPE, NULL, 0, NULL,
                         (SQLPOINTER)&type);
    if (!SQL_SUCCEEDED(rc) || type == SQL_UNKNOWN_TYPE) {
        /* MANY ODBC v2 datasources only supply CONCISE_TYPE */
        rc = SQLColAttribute(stmt, icol + 1, SQL_DESC_CONCISE_TYPE, NULL,
                             0, NULL, (SQLPOINTER)&type);
    }

    if (!SQL_SUCCEEDED(rc)) {
        /* if still unknown make it CHAR */
        type = SQL_C_CHAR;
    }

    switch (type) {
    case SQL_INTEGER:
    case SQL_SMALLINT:
    case SQL_TINYINT:
    case SQL_BIGINT:
      /* fix these numeric binary types up as signed/unsigned for C types */
      type += (isunsigned) ? SQL_UNSIGNED_OFFSET : SQL_SIGNED_OFFSET;
      break;
    /* LOB types are not changed to C types */
    case SQL_LONGVARCHAR: 
        type = SQL_LONGVARCHAR; 
        break;
    case SQL_LONGVARBINARY: 
        type = SQL_LONGVARBINARY; 
        break;
    case SQL_FLOAT : 
        type = SQL_C_FLOAT; 
        break;
    case SQL_DOUBLE : 
        type = SQL_C_DOUBLE; 
        break;

    /* DBD wants times as strings */
    case SQL_TIMESTAMP:      
    case SQL_DATE:
    case SQL_TIME:
    default:
      type = SQL_C_CHAR;
    }

    res->coltypes[icol] = (SQLSMALLINT)type;

    /* size if retrieved as text */
    rc = SQLColAttribute(stmt, icol + 1, SQL_DESC_DISPLAY_SIZE, NULL, 0,
                         NULL, (SQLPOINTER)&textsize);
    if (!SQL_SUCCEEDED(rc) || textsize < 0) {
        textsize = res->apr_dbd->defaultBufferSize;
    }
    /* for null-term, which sometimes isn't included */
    textsize++;

    /* real size */
    rc = SQLColAttribute(stmt, icol + 1, SQL_DESC_OCTET_LENGTH, NULL, 0,
                         NULL, (SQLPOINTER)&realsize);
    if (!SQL_SUCCEEDED(rc)) {
        realsize = textsize;
    }

    maxsize = (textsize > realsize) ? textsize : realsize;
    if (IS_LOB(type) || maxsize <= 0) {
        /* LOB types are never bound and have a NULL colptr for binary.
         * Ingore their real (1-2gb) length & use a default - the larger
         * of defaultBufferSize or APR_BUCKET_BUFF_SIZE.
         * If not a LOB, but simply unknown length - always use defaultBufferSize.
         */
        maxsize = res->apr_dbd->defaultBufferSize;
        if (IS_LOB(type) && maxsize < APR_BUCKET_BUFF_SIZE) {
            maxsize = APR_BUCKET_BUFF_SIZE;
        }

        res->colptrs[icol] =  NULL;
        res->colstate[icol] = COL_AVAIL;
        res->colsizes[icol] = (SQLINTEGER)maxsize;
        rc = SQL_SUCCESS;
    }
    else {
        res->colptrs[icol] = apr_pcalloc(res->pool, maxsize);
        res->colsizes[icol] = (SQLINTEGER)maxsize;
        if (res->apr_dbd->dboptions & SQL_GD_BOUND) {
            /* we are allowed to call SQLGetData if we need to */
            rc = SQLBindCol(stmt, icol + 1, res->coltypes[icol], 
                            res->colptrs[icol], maxsize, 
                            &(res->colinds[icol]));
            CHECK_ERROR(res->apr_dbd, "SQLBindCol", rc, SQL_HANDLE_STMT,
                        stmt);
            res->colstate[icol] = SQL_SUCCEEDED(rc) ? COL_BOUND : COL_AVAIL;
        }
        else {
            /* this driver won't allow us to call SQLGetData on bound 
             * columns - so don't bind any
             */
            res->colstate[icol] = COL_AVAIL;
            rc = SQL_SUCCESS;
        }
    }
    return rc;
}

/* create and populate an apr_dbd_results_t for a select */
static SQLRETURN odbc_create_results(apr_dbd_t *handle, SQLHANDLE hstmt,
                                     apr_pool_t *pool, const int random,
                                     apr_dbd_results_t **res)
{
    SQLRETURN rc;
    SQLSMALLINT ncols;

    *res = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
    (*res)->stmt = hstmt;
    (*res)->dbc = handle->dbc;
    (*res)->pool = pool;
    (*res)->random = random;
    (*res)->apr_dbd = handle;
    rc = SQLNumResultCols(hstmt, &ncols);
    CHECK_ERROR(handle, "SQLNumResultCols", rc, SQL_HANDLE_STMT, hstmt);
    (*res)->ncols = ncols;

    if (SQL_SUCCEEDED(rc)) {
        int i;

        (*res)->colnames = apr_pcalloc(pool, ncols * sizeof(char *));
        (*res)->colptrs = apr_pcalloc(pool, ncols * sizeof(void *));
        (*res)->colsizes = apr_pcalloc(pool, ncols * sizeof(SQLINTEGER));
        (*res)->coltypes = apr_pcalloc(pool, ncols * sizeof(SQLSMALLINT));
        (*res)->colinds = apr_pcalloc(pool, ncols * sizeof(SQLLEN));
        (*res)->colstate = apr_pcalloc(pool, ncols * sizeof(int));
        (*res)->ncols = ncols;

        for (i = 0; i < ncols; i++) {
            odbc_set_result_column(i, (*res), hstmt);
        }
    }
    return rc;
}


/* bind a parameter - input params only, does not support output parameters */
static SQLRETURN odbc_bind_param(apr_pool_t *pool,
                                 apr_dbd_prepared_t *statement, const int narg,
                                 const SQLSMALLINT type, int *argp,
                                 const void **args, const int textmode)
{
    SQLRETURN rc;
    SQLSMALLINT baseType, cType;
    void *ptr;
    SQLULEN len;
    SQLLEN *indicator;
    static SQLLEN nullValue = SQL_NULL_DATA;
    static SQLSMALLINT inOut = SQL_PARAM_INPUT;     /* only input params */

    /* bind a NULL data value */
    if (args[*argp] == NULL || type == APR_DBD_TYPE_NULL) {
        baseType = SQL_CHAR;
        cType = SQL_C_CHAR;
        ptr = &nullValue;
        len = sizeof(SQLINTEGER);
        indicator = &nullValue;
        (*argp)++;
    }
    /* bind a non-NULL data value */
    else {
        if (type < 0 || type >= NUM_APR_DBD_TYPES) {
            return APR_EGENERAL;
        }

        baseType = sqlBaseType[type];
        cType = sqlCtype[type];
        indicator = NULL;
        /* LOBs */
        if (IS_LOB(cType)) {
            ptr = (void *)args[*argp];
            len = (SQLULEN) * (apr_size_t *)args[*argp + 1];
            cType = (IS_CLOB(cType)) ? SQL_C_CHAR : SQL_C_DEFAULT;
            (*argp) += 4;  /* LOBs consume 4 args (last two are unused) */
        }
        /* non-LOBs */
        else {
            switch (baseType) {
            case SQL_CHAR:
            case SQL_DATE:
            case SQL_TIME:
            case SQL_TIMESTAMP:
                ptr = (void *)args[*argp];
                len = (SQLULEN)strlen(ptr);
                break;
            case SQL_TINYINT:
                ptr = apr_palloc(pool, sizeof(unsigned char));
                len = sizeof(unsigned char);
                *(unsigned char *)ptr =
                    (textmode ?
                     atoi(args[*argp]) : *(unsigned char *)args[*argp]);
                break;
            case SQL_SMALLINT:
                ptr = apr_palloc(pool, sizeof(short));
                len = sizeof(short);
                *(short *)ptr =
                    (textmode ? atoi(args[*argp]) : *(short *)args[*argp]);
                break;
            case SQL_INTEGER:
                ptr = apr_palloc(pool, sizeof(int));
                len = sizeof(int);
                *(long *)ptr =
                    (textmode ? atol(args[*argp]) : *(long *)args[*argp]);
                break;
            case SQL_FLOAT:
                ptr = apr_palloc(pool, sizeof(float));
                len = sizeof(float);
                *(float *)ptr =
                    (textmode ?
                     (float)atof(args[*argp]) : *(float *)args[*argp]);
                break;
            case SQL_DOUBLE:
                ptr = apr_palloc(pool, sizeof(double));
                len = sizeof(double);
                *(double *)ptr =
                    (textmode ? atof(args[*argp]) : *(double *)
                     args[*argp]);
                break;
            case SQL_BIGINT:
                ptr = apr_palloc(pool, sizeof(apr_int64_t));
                len = sizeof(apr_int64_t);
                *(apr_int64_t *)ptr =
                    (textmode ?
                     apr_atoi64(args[*argp]) : *(apr_int64_t *)args[*argp]);
                break;
            default:
                return APR_EGENERAL;
            }
            (*argp)++;          /* non LOBs consume one argument */
        }
    }
    rc = SQLBindParameter(statement->stmt, narg, inOut, cType, 
                          baseType, len, 0, ptr, len, indicator);
    CHECK_ERROR(statement->apr_dbd, "SQLBindParameter", rc, SQL_HANDLE_STMT,
                statement->stmt);
    return rc;
}

/* LOB / Bucket Brigade functions */

/* bucket type specific destroy */
static void odbc_lob_bucket_destroy(void *data)
{
    odbc_bucket *bd = data;

    if (apr_bucket_shared_destroy(bd))
        apr_bucket_free(bd);
}

/* set aside a bucket if possible */
static apr_status_t odbc_lob_bucket_setaside(apr_bucket *e, apr_pool_t *pool)
{
    odbc_bucket *bd = (odbc_bucket *)e->data;

    /* Unlikely - but if the row pool is ancestor of this pool then it is OK */
    if (apr_pool_is_ancestor(bd->row->pool, pool))
        return APR_SUCCESS;
    
    return apr_bucket_setaside_notimpl(e, pool);
}

/* split a bucket into a heap bucket followed by a LOB bkt w/remaining data */
static apr_status_t odbc_lob_bucket_read(apr_bucket *e, const char **str,
                                         apr_size_t *len, apr_read_type_e block)
{
    SQLRETURN rc;
    SQLLEN len_indicator;
    SQLSMALLINT type;
    odbc_bucket *bd = (odbc_bucket *)e->data;
    apr_bucket *nxt;
    void *buf;
    int bufsize = bd->row->res->apr_dbd->defaultBufferSize;
    int eos;
    
    /* C type is CHAR for CLOBs, DEFAULT for BLOBs */
    type = bd->row->res->coltypes[bd->col];
    type = (type == SQL_LONGVARCHAR) ? SQL_C_CHAR : SQL_C_DEFAULT;

    /* LOB buffers are always at least APR_BUCKET_BUFF_SIZE, 
     *   but they may be much bigger per the BUFSIZE parameter.
     */
    if (bufsize < APR_BUCKET_BUFF_SIZE)
        bufsize = APR_BUCKET_BUFF_SIZE;

    buf = apr_bucket_alloc(bufsize, e->list);
    *str = NULL;
    *len = 0;

    rc = SQLGetData(bd->row->res->stmt, bd->col + 1, 
                    type, buf, bufsize, 
                    &len_indicator);

    CHECK_ERROR(bd->row->res->apr_dbd, "SQLGetData", rc, 
                SQL_HANDLE_STMT, bd->row->res->stmt);
    
    if (rc == SQL_NO_DATA || len_indicator == SQL_NULL_DATA || len_indicator < 0)
        len_indicator = 0;

    if (SQL_SUCCEEDED(rc) || rc == SQL_NO_DATA) {

        if (rc == SQL_SUCCESS_WITH_INFO
            && (len_indicator == SQL_NO_TOTAL || len_indicator >= bufsize)) {
            /* not the last read = a full buffer. CLOBs have a null terminator */
            *len = bufsize - (IS_CLOB(bd->type) ? 1 : 0 );

             eos = 0;
        }
        else {
            /* the last read - len_indicator is supposed to be the length, 
             * but some driver get this wrong and return the total length.
             * We try to handle both interpretations.
             */
            *len =  (len_indicator > bufsize 
                     && len_indicator >= (SQLLEN)e->start)
                ? (len_indicator - (SQLLEN)e->start) : len_indicator;

            eos = 1;
        }

        if (!eos) {
            /* Create a new LOB bucket to append and append it */
            nxt = apr_bucket_alloc(sizeof(apr_bucket *), e->list);
            APR_BUCKET_INIT(nxt);
            nxt->length = -1;
            nxt->data   = e->data;
            nxt->type   = &odbc_bucket_type;
            nxt->free   = apr_bucket_free;
            nxt->list   = e->list;
            nxt->start  = e->start + *len;
            APR_BUCKET_INSERT_AFTER(e, nxt);
        }
        else {
            odbc_lob_bucket_destroy(e->data);
        }
        /* make current bucket into a heap bucket */
        apr_bucket_heap_make(e, buf, *len, apr_bucket_free);
        *str = buf;

        /* No data is success in this context */
        rc = SQL_SUCCESS;
    }
    return APR_FROM_SQL_RESULT(rc);
}

/* Create a bucket brigade on the row pool for a LOB column */
static apr_status_t odbc_create_bucket(const apr_dbd_row_t *row, const int col, 
                                       SQLSMALLINT type, apr_bucket_brigade *bb)
{
    apr_bucket_alloc_t *list = bb->bucket_alloc;
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);
    odbc_bucket *bd = apr_bucket_alloc(sizeof(odbc_bucket), list);
    apr_bucket *eos = apr_bucket_eos_create(list);
    
    bd->row = row;
    bd->col = col;
    bd->type = type;

    APR_BUCKET_INIT(b);
    b->type = &odbc_bucket_type;
    b->free = apr_bucket_free;
    b->list = list;
    /* LOB lengths are unknown in ODBC */
    b = apr_bucket_shared_make(b, bd, 0, -1);

    APR_BRIGADE_INSERT_TAIL(bb, b);
    APR_BRIGADE_INSERT_TAIL(bb, eos);

    return APR_SUCCESS;
}

/* returns a data pointer for a column,  returns NULL for NULL value,
 * return -1 if data not available
 */
static void *odbc_get(const apr_dbd_row_t *row, const int col, 
                      const SQLSMALLINT sqltype)
{
    SQLRETURN rc;
    SQLLEN indicator;
    int state = row->res->colstate[col];
    ODBC_INTPTR_T options = row->res->apr_dbd->dboptions;

    switch (state) {
    case (COL_UNAVAIL):
        return (void *)-1;
    case (COL_RETRIEVED):
        return NULL;

    case (COL_BOUND):
    case (COL_PRESENT): 
        if (sqltype == row->res->coltypes[col]) {
            /* same type and we already have the data */
            row->res->colstate[col] = COL_RETRIEVED;
            return (row->res->colinds[col] == SQL_NULL_DATA) ? 
                NULL : row->res->colptrs[col];
        }
    }

    /* we need to get the data now */
    if (!(options & SQL_GD_ANY_ORDER)) {
        /* this ODBC driver requires columns to be retrieved in order,
         * so we attempt to get every prior un-gotten non-LOB column
         */
        int i;
        for (i = 0; i < col; i++) {
            if (row->res->colstate[i] == COL_AVAIL) {
                if (IS_LOB(row->res->coltypes[i]))
                       row->res->colstate[i] = COL_UNAVAIL;
                else {
                    odbc_get(row, i, row->res->coltypes[i]);
                    row->res->colstate[i] = COL_PRESENT;
                }
            }
        }
    }

    if ((state == COL_BOUND && !(options & SQL_GD_BOUND)))
        /* this driver won't let us re-get bound columns */
        return (void *)-1;

    /* a LOB might not have a buffer allocated yet - so create one */
    if (!row->res->colptrs[col])
        row->res->colptrs[col] = apr_pcalloc(row->pool, row->res->colsizes[col]);

    rc = SQLGetData(row->res->stmt, col + 1, sqltype, row->res->colptrs[col],
                    row->res->colsizes[col], &indicator);
    CHECK_ERROR(row->res->apr_dbd, "SQLGetData", rc, SQL_HANDLE_STMT, 
                row->res->stmt);
    if (indicator == SQL_NULL_DATA || rc == SQL_NO_DATA)
        return NULL;

    if (SQL_SUCCEEDED(rc)) {
        /* whatever it was originally, it is now this sqltype */
        row->res->coltypes[col] = sqltype;
        /* this allows getting CLOBs in text mode by calling get_entry
         *   until it returns NULL
         */
        row->res->colstate[col] = 
            (rc == SQL_SUCCESS_WITH_INFO) ? COL_AVAIL : COL_RETRIEVED;
        return row->res->colptrs[col];
    }
    else
        return (void *)-1;
}

/* Parse the parameter string for open */
static apr_status_t odbc_parse_params(apr_pool_t *pool, const char *params,
                               int *connect, SQLCHAR **datasource, 
                               SQLCHAR **user, SQLCHAR **password, 
                               int *defaultBufferSize, int *nattrs,
                               int **attrs, ODBC_INTPTR_T **attrvals)
{
    char *seps, *last, *next, *name[MAX_PARAMS], *val[MAX_PARAMS];
    int nparams = 0, i, j;

    *attrs = apr_pcalloc(pool, MAX_PARAMS * sizeof(char *));
    *attrvals = apr_pcalloc(pool, MAX_PARAMS * sizeof(ODBC_INTPTR_T));
    *nattrs = 0;
    seps = DEFAULTSEPS;
    name[nparams] = apr_strtok(apr_pstrdup(pool, params), seps, &last);

    /* no params is OK here - let connect return a more useful error msg */
    if (!name[nparams])
        return SQL_SUCCESS;

    do {
        if (last[strspn(last, seps)] == CSINGLEQUOTE) {
            last += strspn(last, seps);
            seps=SSINGLEQUOTE;
        }
        val[nparams] = apr_strtok(NULL, seps, &last);
        seps = DEFAULTSEPS;

        ++nparams;
        next = apr_strtok(NULL, seps, &last);
        if (!next) {
            break;
        }
        if (nparams >= MAX_PARAMS) {
            /* too many parameters, no place to store */
            return APR_EGENERAL;
        }
        name[nparams] = next;
    } while (1);

    for (j = i = 0; i < nparams; i++) {
        if (!apr_strnatcasecmp(name[i], "CONNECT")) {
            *datasource = (SQLCHAR *)apr_pstrdup(pool, val[i]);
            *connect = 1;
        }
        else if (!apr_strnatcasecmp(name[i], "DATASOURCE")) {
            *datasource = (SQLCHAR *)apr_pstrdup(pool, val[i]);
            *connect = 0;
        }
        else if (!apr_strnatcasecmp(name[i], "USER")) {
            *user = (SQLCHAR *)apr_pstrdup(pool, val[i]);
        }
        else if (!apr_strnatcasecmp(name[i], "PASSWORD")) {
            *password = (SQLCHAR *)apr_pstrdup(pool, val[i]);
        }
        else if (!apr_strnatcasecmp(name[i], "BUFSIZE")) {
            *defaultBufferSize = atoi(val[i]);
        }
        else if (!apr_strnatcasecmp(name[i], "ACCESS")) {
            if (!apr_strnatcasecmp(val[i], "READ_ONLY"))
                (*attrvals)[j] = SQL_MODE_READ_ONLY;
            else if (!apr_strnatcasecmp(val[i], "READ_WRITE"))
                (*attrvals)[j] = SQL_MODE_READ_WRITE;
            else
                return SQL_ERROR;
            (*attrs)[j++] = SQL_ATTR_ACCESS_MODE;
        }
        else if (!apr_strnatcasecmp(name[i], "CTIMEOUT")) {
            (*attrvals)[j] = atoi(val[i]);
            (*attrs)[j++] = SQL_ATTR_LOGIN_TIMEOUT;
        }
        else if (!apr_strnatcasecmp(name[i], "STIMEOUT")) {
            (*attrvals)[j] = atoi(val[i]);
            (*attrs)[j++] = SQL_ATTR_CONNECTION_TIMEOUT;
        }
        else if (!apr_strnatcasecmp(name[i], "TXMODE")) {
            if (!apr_strnatcasecmp(val[i], "READ_UNCOMMITTED"))
                (*attrvals)[j] = SQL_TXN_READ_UNCOMMITTED;
            else if (!apr_strnatcasecmp(val[i], "READ_COMMITTED"))
                (*attrvals)[j] = SQL_TXN_READ_COMMITTED;
            else if (!apr_strnatcasecmp(val[i], "REPEATABLE_READ"))
                (*attrvals)[j] = SQL_TXN_REPEATABLE_READ;
            else if (!apr_strnatcasecmp(val[i], "SERIALIZABLE"))
                (*attrvals)[j] = SQL_TXN_SERIALIZABLE;
            else if (!apr_strnatcasecmp(val[i], "DEFAULT"))
                continue;
            else
                return SQL_ERROR;
            (*attrs)[j++] = SQL_ATTR_TXN_ISOLATION;
        }
        else
            return SQL_ERROR;
    }
    *nattrs = j;
    return (*datasource && *defaultBufferSize) ? APR_SUCCESS : SQL_ERROR;
}

/* common handling after ODBC calls - save error info (code and text) in dbc */
static void check_error(apr_dbd_t *dbc, const char *step, SQLRETURN rc,
                 SQLSMALLINT type, SQLHANDLE h, int line)
{
    SQLCHAR buffer[512];
    SQLCHAR sqlstate[128];
    SQLINTEGER native;
    SQLSMALLINT reslength;
    char *res, *p, *end, *logval = NULL;
    int i;

    /* set info about last error in dbc  - fast return for SQL_SUCCESS  */
    if (rc == SQL_SUCCESS) {
        char successMsg[] = "[dbd_odbc] SQL_SUCCESS ";
        apr_size_t successMsgLen = sizeof successMsg - 1;

        dbc->lasterrorcode = SQL_SUCCESS;
        apr_cpystrn(dbc->lastError, successMsg, sizeof dbc->lastError);
        apr_cpystrn(dbc->lastError + successMsgLen, step,
                    sizeof dbc->lastError - successMsgLen);
        return;
    }
    switch (rc) {
    case SQL_INVALID_HANDLE:
        res = "SQL_INVALID_HANDLE";
        break;
    case SQL_ERROR:
        res = "SQL_ERROR";
        break;
    case SQL_SUCCESS_WITH_INFO:
        res = "SQL_SUCCESS_WITH_INFO";
        break;
    case SQL_STILL_EXECUTING:
        res = "SQL_STILL_EXECUTING";
        break;
    case SQL_NEED_DATA:
        res = "SQL_NEED_DATA";
        break;
    case SQL_NO_DATA:
        res = "SQL_NO_DATA";
        break;
    default:
        res = "unrecognized SQL return code";
    }
    /* these two returns are expected during normal execution */
    if (rc != SQL_SUCCESS_WITH_INFO && rc != SQL_NO_DATA 
        && dbc->can_commit != APR_DBD_TRANSACTION_IGNORE_ERRORS) {
        dbc->can_commit = APR_DBD_TRANSACTION_ROLLBACK;
    }
    p = dbc->lastError;
    end = p + sizeof(dbc->lastError);
    dbc->lasterrorcode = rc;
    p += sprintf(p, "[dbd_odbc] %.64s returned %.30s (%d) at %.24s:%d ",
                 step, res, rc, SOURCE_FILE, line - 1);
    for (i = 1, rc = 0; rc == 0; i++) {
        rc = SQLGetDiagRec(type, h, i, sqlstate, &native, buffer, 
                            sizeof(buffer), &reslength);
        if (SQL_SUCCEEDED(rc) && (p < (end - 280))) 
            p += sprintf(p, "%.256s %.20s ", buffer, sqlstate);
    }
    apr_env_get(&logval, "apr_dbd_odbc_log", dbc->pool);
    /* if env var was set or call was init/open (no dbname) - log to stderr */
    if (logval || !dbc->dbname ) {
        char timestamp[APR_CTIME_LEN];

        apr_file_t *se;
        apr_ctime(timestamp, apr_time_now());
        apr_file_open_stderr(&se, dbc->pool);
        apr_file_printf(se, "[%s] %s\n", timestamp, dbc->lastError);
    }
}

static APR_INLINE int odbc_check_rollback(apr_dbd_t *handle)
{
    if (handle->can_commit == APR_DBD_TRANSACTION_ROLLBACK) {
        handle->lasterrorcode = SQL_ERROR;
        apr_cpystrn(handle->lastError, "[dbd_odbc] Rollback pending ",
                    sizeof handle->lastError);
        return 1;
    }
    return 0;
}

/*
 *   public functions per DBD driver API
 */

/** init: allow driver to perform once-only initialisation. **/
static void odbc_init(apr_pool_t *pool)
{
    SQLRETURN rc;
    char *step;
    apr_version_t apuver;
    
    apu_version(&apuver);
    if (apuver.major != DRIVER_APU_VERSION_MAJOR 
        || apuver.minor != DRIVER_APU_VERSION_MINOR) {
            apr_file_t *se;

            apr_file_open_stderr(&se, pool);
            apr_file_printf(se, "Incorrect " ODBC_DRIVER_STRING " dbd driver version\n"
                "Attempt to load APU version %d.%d driver with APU version %d.%d\n",
                DRIVER_APU_VERSION_MAJOR, DRIVER_APU_VERSION_MINOR, 
                apuver.major, apuver.minor);
        abort();
    }

    if (henv) 
        return;

    step = "SQLAllocHandle (SQL_HANDLE_ENV)";
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    apr_pool_cleanup_register(pool, henv, odbc_close_env, apr_pool_cleanup_null);
    if (SQL_SUCCEEDED(rc)) {
        step = "SQLSetEnvAttr";
        rc = SQLSetEnvAttr(henv,SQL_ATTR_ODBC_VERSION,
                          (SQLPOINTER)SQL_OV_ODBC3, 0);
    }
    else {
        apr_dbd_t tmp_dbc;
        SQLHANDLE err_h = henv;

        tmp_dbc.pool = pool;
        tmp_dbc.dbname = NULL;
        CHECK_ERROR(&tmp_dbc, step, rc, SQL_HANDLE_ENV, err_h);
    }
}

/** native_handle: return the native database handle of the underlying db **/
static void *odbc_native_handle(apr_dbd_t *handle)
{
    return handle->dbc;
}

/** open: obtain a database connection from the server rec. **/

/* It would be more efficient to allocate a single statement handle 
 * here - but SQL_ATTR_CURSOR_SCROLLABLE must be set before
 * SQLPrepare, and we don't know whether random-access is
 * specified until SQLExecute so we cannot.
 */

static apr_dbd_t *odbc_open(apr_pool_t *pool, const char *params, const char **error)
{
    SQLRETURN rc;
    SQLHANDLE hdbc = NULL;
    apr_dbd_t *handle;
    char *err_step;
    int err_htype, i;
    int defaultBufferSize = DEFAULT_BUFFER_SIZE;
    SQLHANDLE err_h = NULL;
    SQLCHAR  *datasource = (SQLCHAR *)"", *user = (SQLCHAR *)"",
             *password = (SQLCHAR *)"";
    int nattrs = 0, *attrs = NULL,  connect = 0;
    ODBC_INTPTR_T *attrvals = NULL;

    err_step = "SQLAllocHandle (SQL_HANDLE_DBC)";
    err_htype = SQL_HANDLE_ENV;
    err_h = henv;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (SQL_SUCCEEDED(rc)) {
        err_step = "Invalid DBD Parameters - open";
        err_htype = SQL_HANDLE_DBC;
        err_h = hdbc;
        rc = odbc_parse_params(pool, params, &connect, &datasource, &user,
                               &password, &defaultBufferSize, &nattrs, &attrs,
                               &attrvals);
    }
    if (SQL_SUCCEEDED(rc)) {
        for (i = 0; i < nattrs && SQL_SUCCEEDED(rc); i++) {
            err_step = "SQLSetConnectAttr (from DBD Parameters)";
            err_htype = SQL_HANDLE_DBC;
            err_h = hdbc;
            rc = SQLSetConnectAttr(hdbc, attrs[i], (SQLPOINTER)attrvals[i], 0);
        }
    }
    if (SQL_SUCCEEDED(rc)) {
        if (connect) {
            SQLCHAR out[1024];
            SQLSMALLINT outlen;

            err_step = "SQLDriverConnect";
            err_htype = SQL_HANDLE_DBC;
            err_h = hdbc;
            rc = SQLDriverConnect(hdbc, NULL, datasource,
                        (SQLSMALLINT)strlen((char *)datasource),
                        out, sizeof(out), &outlen, SQL_DRIVER_NOPROMPT);
        }
        else {
            err_step = "SQLConnect";
            err_htype = SQL_HANDLE_DBC;
            err_h = hdbc;
            rc = SQLConnect(hdbc, datasource,
                        (SQLSMALLINT)strlen((char *)datasource),
                        user, (SQLSMALLINT)strlen((char *)user),
                        password, (SQLSMALLINT)strlen((char *)password));
        }
    }
    if (SQL_SUCCEEDED(rc)) {
        handle = apr_pcalloc(pool, sizeof(apr_dbd_t));
        handle->dbname = apr_pstrdup(pool, (char *)datasource);
        handle->dbc = hdbc;
        handle->pool = pool;
        handle->defaultBufferSize = defaultBufferSize;
        CHECK_ERROR(handle, "SQLConnect", rc, SQL_HANDLE_DBC, handle->dbc);
        handle->default_transaction_mode = 0;
        handle->can_commit = APR_DBD_TRANSACTION_IGNORE_ERRORS;
        SQLGetInfo(hdbc, SQL_DEFAULT_TXN_ISOLATION,
                   &(handle->default_transaction_mode), sizeof(ODBC_INTPTR_T), NULL);
        handle->transaction_mode = handle->default_transaction_mode;
        SQLGetInfo(hdbc, SQL_GETDATA_EXTENSIONS ,&(handle->dboptions),
                   sizeof(ODBC_INTPTR_T), NULL);
        apr_pool_cleanup_register(pool, handle, odbc_close_cleanup, apr_pool_cleanup_null);
        return handle;
    }
    else {
        apr_dbd_t tmp_dbc;

        tmp_dbc.pool = pool;
        tmp_dbc.dbname = NULL;
        CHECK_ERROR(&tmp_dbc, err_step, rc, err_htype, err_h);
        if (error)
            *error = apr_pstrdup(pool, tmp_dbc.lastError);
        if (hdbc)
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        return NULL;
    }
}

/** check_conn: check status of a database connection **/
static apr_status_t odbc_check_conn(apr_pool_t *pool, apr_dbd_t *handle)
{
    SQLUINTEGER isDead;
    SQLRETURN   rc;

    rc = SQLGetConnectAttr(handle->dbc, SQL_ATTR_CONNECTION_DEAD, &isDead,
                            sizeof(SQLUINTEGER), NULL);
    CHECK_ERROR(handle, "SQLGetConnectAttr (SQL_ATTR_CONNECTION_DEAD)", rc,
                SQL_HANDLE_DBC, handle->dbc);
    /* if driver cannot check connection, say so */
    if (rc != SQL_SUCCESS)
        return APR_ENOTIMPL;

    return (isDead == SQL_CD_FALSE) ? APR_SUCCESS : APR_EGENERAL;
}

/** set_dbname: select database name.  May be a no-op if not supported. **/
static int odbc_set_dbname(apr_pool_t*pool, apr_dbd_t *handle,
                           const char *name)
{
    if (apr_strnatcmp(name, handle->dbname)) {
        return APR_EGENERAL;        /* It's illegal to change dbname in ODBC */
    }
    CHECK_ERROR(handle, "set_dbname (no-op)", SQL_SUCCESS, SQL_HANDLE_DBC,
                handle->dbc);
    return APR_SUCCESS;             /* OK if it's the same name */
}

/** transaction: start a transaction.  May be a no-op. **/
static int odbc_start_transaction(apr_pool_t *pool, apr_dbd_t *handle,
                                  apr_dbd_transaction_t **trans)
{
    SQLRETURN rc = SQL_SUCCESS;

    if (handle->transaction_mode) {
        rc = SQLSetConnectAttr(handle->dbc, SQL_ATTR_TXN_ISOLATION,
                               (SQLPOINTER)handle->transaction_mode, 0);
        CHECK_ERROR(handle, "SQLSetConnectAttr (SQL_ATTR_TXN_ISOLATION)", rc,
                    SQL_HANDLE_DBC, handle->dbc);
    }
    if (SQL_SUCCEEDED(rc)) {
        /* turn off autocommit for transactions */
        rc = SQLSetConnectAttr(handle->dbc, SQL_ATTR_AUTOCOMMIT,
                               SQL_AUTOCOMMIT_OFF, 0);
        CHECK_ERROR(handle, "SQLSetConnectAttr (SQL_ATTR_AUTOCOMMIT)", rc,
                    SQL_HANDLE_DBC, handle->dbc);
    }
    if (SQL_SUCCEEDED(rc)) {
        *trans = apr_palloc(pool, sizeof(apr_dbd_transaction_t));
        (*trans)->dbc = handle->dbc;
        (*trans)->apr_dbd = handle;
    }
    handle->can_commit = APR_DBD_TRANSACTION_COMMIT;
    return APR_FROM_SQL_RESULT(rc);
}

/** end_transaction: end a transaction **/
static int odbc_end_transaction(apr_dbd_transaction_t *trans)
{
    SQLRETURN rc;
    int action = (trans->apr_dbd->can_commit != APR_DBD_TRANSACTION_ROLLBACK) 
        ? SQL_COMMIT : SQL_ROLLBACK;

    rc = SQLEndTran(SQL_HANDLE_DBC, trans->dbc, action);
    CHECK_ERROR(trans->apr_dbd, "SQLEndTran", rc, SQL_HANDLE_DBC, trans->dbc);
    if (SQL_SUCCEEDED(rc)) {
        rc = SQLSetConnectAttr(trans->dbc, SQL_ATTR_AUTOCOMMIT,
                               (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
        CHECK_ERROR(trans->apr_dbd, "SQLSetConnectAttr (SQL_ATTR_AUTOCOMMIT)",
                    rc, SQL_HANDLE_DBC, trans->dbc);
    }
    trans->apr_dbd->can_commit = APR_DBD_TRANSACTION_IGNORE_ERRORS;
    return APR_FROM_SQL_RESULT(rc);
}

/** query: execute an SQL statement which doesn't return a result set **/
static int odbc_query(apr_dbd_t *handle, int *nrows, const char *statement)
{
    SQLRETURN rc;
    SQLHANDLE hstmt = NULL;
    size_t len = strlen(statement);

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, handle->dbc, &hstmt);
    CHECK_ERROR(handle, "SQLAllocHandle (STMT)", rc, SQL_HANDLE_DBC,
                handle->dbc);
    if (!SQL_SUCCEEDED(rc))
        return APR_FROM_SQL_RESULT(rc);

    rc = SQLExecDirect(hstmt, (SQLCHAR *)statement, (SQLINTEGER)len);
    CHECK_ERROR(handle, "SQLExecDirect", rc, SQL_HANDLE_STMT, hstmt);

    if (SQL_SUCCEEDED(rc)) {
        SQLLEN rowcount;

        rc = SQLRowCount(hstmt, &rowcount);
        *nrows = (int)rowcount;
        CHECK_ERROR(handle, "SQLRowCount", rc, SQL_HANDLE_STMT, hstmt);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return APR_FROM_SQL_RESULT(rc);
}

/** select: execute an SQL statement which returns a result set **/
static int odbc_select(apr_pool_t *pool, apr_dbd_t *handle,
                       apr_dbd_results_t **res, const char *statement,
                       int random)
{
    SQLRETURN rc;
    SQLHANDLE hstmt;
    apr_dbd_prepared_t *stmt;
    size_t len = strlen(statement);

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, handle->dbc, &hstmt);
    CHECK_ERROR(handle, "SQLAllocHandle (STMT)", rc, SQL_HANDLE_DBC,
                handle->dbc);
    if (!SQL_SUCCEEDED(rc))
        return APR_FROM_SQL_RESULT(rc);
    /* Prepare an apr_dbd_prepared_t for pool cleanup, even though this
     * is not a prepared statement.  We want the same cleanup mechanism.
     */
    stmt = apr_pcalloc(pool, sizeof(apr_dbd_prepared_t));
    stmt->apr_dbd = handle;
    stmt->dbc = handle->dbc;
    stmt->stmt = hstmt;
    apr_pool_cleanup_register(pool, stmt, odbc_close_pstmt, apr_pool_cleanup_null);
    if (random) {
        rc = SQLSetStmtAttr(hstmt, SQL_ATTR_CURSOR_SCROLLABLE,
                            (SQLPOINTER)SQL_SCROLLABLE, 0);
        CHECK_ERROR(handle, "SQLSetStmtAttr (SQL_ATTR_CURSOR_SCROLLABLE)", rc,
                    SQL_HANDLE_STMT, hstmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = SQLExecDirect(hstmt, (SQLCHAR *)statement, (SQLINTEGER)len);
        CHECK_ERROR(handle, "SQLExecDirect", rc, SQL_HANDLE_STMT, hstmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = odbc_create_results(handle, hstmt, pool, random, res);
        apr_pool_cleanup_register(pool, *res, 
                                  odbc_close_results, apr_pool_cleanup_null);
    }
    return APR_FROM_SQL_RESULT(rc);
}

/** num_cols: get the number of columns in a results set **/
static int odbc_num_cols(apr_dbd_results_t *res)
{
    return res->ncols;
}

/** num_tuples: get the number of rows in a results set **/
static int odbc_num_tuples(apr_dbd_results_t *res)
{
    SQLRETURN rc;
    SQLLEN nrows;

    rc = SQLRowCount(res->stmt, &nrows);
    CHECK_ERROR(res->apr_dbd, "SQLRowCount", rc, SQL_HANDLE_STMT, res->stmt);
    return SQL_SUCCEEDED(rc) ? (int)nrows : -1;
}

/** get_row: get a row from a result set **/
static int odbc_get_row(apr_pool_t *pool, apr_dbd_results_t *res,
                        apr_dbd_row_t **row, int rownum)
{
    SQLRETURN rc;
    char *fetchtype;
    int c;

    *row = apr_pcalloc(pool, sizeof(apr_dbd_row_t));
    (*row)->stmt = res->stmt;
    (*row)->dbc = res->dbc;
    (*row)->res = res;
    (*row)->pool = res->pool;

    /* mark all the columns as needing SQLGetData unless they are bound  */
    for (c = 0; c < res->ncols; c++) {
        if (res->colstate[c] != COL_BOUND) {
            res->colstate[c] = COL_AVAIL;
        }
        /* some drivers do not null-term zero-len CHAR data */
        if (res->colptrs[c])
            *(char *)res->colptrs[c] = 0; 
    }

    if (res->random && (rownum > 0)) {
        fetchtype = "SQLFetchScroll";
        rc = SQLFetchScroll(res->stmt, SQL_FETCH_ABSOLUTE, rownum);
    }
    else {
        fetchtype = "SQLFetch";
        rc = SQLFetch(res->stmt);
    }
    CHECK_ERROR(res->apr_dbd, fetchtype, rc, SQL_HANDLE_STMT, res->stmt);
    (*row)->stmt = res->stmt;
    if (!SQL_SUCCEEDED(rc) && !res->random) {
        /* early close on any error (usually SQL_NO_DATA) if fetching
         * sequentially to release resources ASAP
         */
        odbc_close_results(res);
        return -1;
    }
    return SQL_SUCCEEDED(rc) ? 0 : -1;
}

/** datum_get: get a binary entry from a row **/
static apr_status_t odbc_datum_get(const apr_dbd_row_t *row, int col,
                                   apr_dbd_type_e dbdtype, void *data)
{
    SQLSMALLINT sqltype;
    void *p;
    int len;

    if (col >= row->res->ncols)
        return APR_EGENERAL;

    if (dbdtype < 0 || dbdtype >= NUM_APR_DBD_TYPES) {
        data = NULL;            /* invalid type */
        return APR_EGENERAL;
    }

    len = sqlSizes[dbdtype];
    sqltype = sqlCtype[dbdtype];

    /* must not memcpy a brigade, sentinals are relative to orig loc */
    if (IS_LOB(sqltype)) 
        return odbc_create_bucket(row, col, sqltype, data);

    p = odbc_get(row, col, sqltype);
    if (p == (void *)-1)
        return APR_EGENERAL;

    if (p == NULL)
        return APR_ENOENT;          /* SQL NULL value */
    
    if (len < 0)
       *(char**)data = (char *)p;
    else
        memcpy(data, p, len);
    
    return APR_SUCCESS;

}

/** get_entry: get an entry from a row (string data) **/
static const char *odbc_get_entry(const apr_dbd_row_t *row, int col)
{
    void *p;

    if (col >= row->res->ncols)
        return NULL;

    p = odbc_get(row, col, SQL_C_CHAR);

    /* NULL or invalid (-1) */
    if (p == NULL || p == (void *)-1)
        return p;     
    else
        return apr_pstrdup(row->pool, p);   
}

/** error: get current error message (if any) **/
static const char *odbc_error(apr_dbd_t *handle, int errnum)
{   
    return (handle) ? handle->lastError : "[dbd_odbc]No error message available";
}

/** escape: escape a string so it is safe for use in query/select **/
static const char *odbc_escape(apr_pool_t *pool, const char *s,
                               apr_dbd_t *handle)
{   
    char *newstr, *src, *dst, *sq;
    int qcount;

    /* return the original if there are no single-quotes */
    if (!(sq = strchr(s, '\''))) 
        return (char *)s;
    /* count the single-quotes and allocate a new buffer */
    for (qcount = 1; (sq = strchr(sq + 1, '\'')); )
        qcount++;
    newstr = apr_palloc(pool, strlen(s) + qcount + 1);

    /* move chars, doubling all single-quotes */
    src = (char *)s;
    for (dst = newstr; *src; src++) {
        if ((*dst++ = *src) == '\'')  
            *dst++ = '\'';
    }
    *dst = 0;
    return newstr;
}

/** prepare: prepare a statement **/
static int odbc_prepare(apr_pool_t *pool, apr_dbd_t *handle,
                        const char *query, const char *label, int nargs,
                        int nvals, apr_dbd_type_e *types,
                        apr_dbd_prepared_t **statement)
{
    SQLRETURN rc;
    size_t len = strlen(query);

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    *statement = apr_pcalloc(pool, sizeof(apr_dbd_prepared_t));
    (*statement)->dbc = handle->dbc;
    (*statement)->apr_dbd = handle;
    (*statement)->nargs = nargs;
    (*statement)->nvals = nvals;
    (*statement)->types =
        apr_pmemdup(pool, types, nargs * sizeof(apr_dbd_type_e));
    rc = SQLAllocHandle(SQL_HANDLE_STMT, handle->dbc, &((*statement)->stmt));
    apr_pool_cleanup_register(pool, *statement, 
                              odbc_close_pstmt, apr_pool_cleanup_null);
    CHECK_ERROR(handle, "SQLAllocHandle (STMT)", rc,
                SQL_HANDLE_DBC, handle->dbc);
    rc = SQLPrepare((*statement)->stmt, (SQLCHAR *)query, (SQLINTEGER)len);
    CHECK_ERROR(handle, "SQLPrepare", rc, SQL_HANDLE_STMT,
                (*statement)->stmt);
    return APR_FROM_SQL_RESULT(rc);
}

/** pquery: query using a prepared statement + args **/
static int odbc_pquery(apr_pool_t *pool, apr_dbd_t *handle, int *nrows,
                       apr_dbd_prepared_t *statement, const char **args)
{
    SQLRETURN rc = SQL_SUCCESS;
    int i, argp;

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    for (i = argp = 0; i < statement->nargs && SQL_SUCCEEDED(rc); i++) {
        rc = odbc_bind_param(pool, statement, i + 1, statement->types[i],
                             &argp, (const void **)args, TEXTMODE);
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = SQLExecute(statement->stmt);
        CHECK_ERROR(handle, "SQLExecute", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        SQLLEN rowcount;

        rc = SQLRowCount(statement->stmt, &rowcount);
        *nrows = (int)rowcount;
        CHECK_ERROR(handle, "SQLRowCount", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    return APR_FROM_SQL_RESULT(rc);
}

/** pvquery: query using a prepared statement + args **/
static int odbc_pvquery(apr_pool_t *pool, apr_dbd_t *handle, int *nrows,
                        apr_dbd_prepared_t *statement, va_list args)
{
    const char **values;
    int i;

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);
    for (i = 0; i < statement->nvals; i++)
        values[i] = va_arg(args, const char *);
    return odbc_pquery(pool, handle, nrows, statement, values);
}

/** pselect: select using a prepared statement + args **/
static int odbc_pselect(apr_pool_t *pool, apr_dbd_t *handle,
                        apr_dbd_results_t **res, apr_dbd_prepared_t *statement,
                        int random, const char **args)
{
    SQLRETURN rc = SQL_SUCCESS;
    int i, argp;

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    if (random) {
        rc = SQLSetStmtAttr(statement->stmt, SQL_ATTR_CURSOR_SCROLLABLE,
                            (SQLPOINTER)SQL_SCROLLABLE, 0);
        CHECK_ERROR(handle, "SQLSetStmtAttr (SQL_ATTR_CURSOR_SCROLLABLE)",
                    rc, SQL_HANDLE_STMT, statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        for (i = argp = 0; i < statement->nargs && SQL_SUCCEEDED(rc); i++) {
            rc = odbc_bind_param(pool, statement, i + 1, statement->types[i],
                                 &argp, (const void **)args, TEXTMODE);
        }
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = SQLExecute(statement->stmt);
        CHECK_ERROR(handle, "SQLExecute", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = odbc_create_results(handle, statement->stmt, pool, random, res);
        apr_pool_cleanup_register(pool, *res,
                                  odbc_close_results, apr_pool_cleanup_null);
    }
    return APR_FROM_SQL_RESULT(rc);
}

/** pvselect: select using a prepared statement + args **/
static int odbc_pvselect(apr_pool_t *pool, apr_dbd_t *handle,
                         apr_dbd_results_t **res,
                         apr_dbd_prepared_t *statement, int random,
                         va_list args)
{
    const char **values;
    int i;

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);
    for (i = 0; i < statement->nvals; i++)
        values[i] = va_arg(args, const char *);
    return odbc_pselect(pool, handle, res, statement, random, values);
}

/** get_name: get a column title from a result set **/
static const char *odbc_get_name(const apr_dbd_results_t *res, int col)
{
    SQLRETURN rc;
    char buffer[MAX_COLUMN_NAME];
    SQLSMALLINT colnamelength, coltype, coldecimal, colnullable;
    SQLULEN colsize;

    if (col >= res->ncols)
        return NULL;            /* bogus column number */
    if (res->colnames[col] != NULL)
        return res->colnames[col];      /* we already retrieved it */
    rc = SQLDescribeCol(res->stmt, col + 1,
                        (SQLCHAR *)buffer, sizeof(buffer), &colnamelength,
                        &coltype, &colsize, &coldecimal, &colnullable);
    CHECK_ERROR(res->apr_dbd, "SQLDescribeCol", rc,
                SQL_HANDLE_STMT, res->stmt);
    res->colnames[col] = apr_pstrdup(res->pool, buffer);
    return res->colnames[col];
}

/** transaction_mode_get: get the mode of transaction **/
static int odbc_transaction_mode_get(apr_dbd_transaction_t *trans)
{
    return (int)trans->apr_dbd->can_commit;
}

/** transaction_mode_set: set the mode of transaction **/
static int odbc_transaction_mode_set(apr_dbd_transaction_t *trans, int mode)
{
    int legal = (  APR_DBD_TRANSACTION_IGNORE_ERRORS
                 | APR_DBD_TRANSACTION_COMMIT
                 | APR_DBD_TRANSACTION_ROLLBACK);

    if ((mode & legal) != mode)
        return APR_EGENERAL;

    trans->apr_dbd->can_commit = mode;
    return APR_SUCCESS;
}

/** pbquery: query using a prepared statement + binary args **/
static int odbc_pbquery(apr_pool_t *pool, apr_dbd_t *handle, int *nrows,
                        apr_dbd_prepared_t *statement, const void **args)
{
    SQLRETURN rc = SQL_SUCCESS;
    int i, argp;

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    for (i = argp = 0; i < statement->nargs && SQL_SUCCEEDED(rc); i++)
        rc = odbc_bind_param(pool, statement, i + 1, statement->types[i],
                             &argp, args, BINARYMODE);

    if (SQL_SUCCEEDED(rc)) {
        rc = SQLExecute(statement->stmt);
        CHECK_ERROR(handle, "SQLExecute", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        SQLLEN rowcount;

        rc = SQLRowCount(statement->stmt, &rowcount);
        *nrows = (int)rowcount;
        CHECK_ERROR(handle, "SQLRowCount", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    return APR_FROM_SQL_RESULT(rc);
}

/** pbselect: select using a prepared statement + binary args **/
static int odbc_pbselect(apr_pool_t *pool, apr_dbd_t *handle,
                         apr_dbd_results_t **res,
                         apr_dbd_prepared_t *statement,
                         int random, const void **args)
{
    SQLRETURN rc = SQL_SUCCESS;
    int i, argp;

    if (odbc_check_rollback(handle))
        return APR_EGENERAL;

    if (random) {
        rc = SQLSetStmtAttr(statement->stmt, SQL_ATTR_CURSOR_SCROLLABLE,
                            (SQLPOINTER)SQL_SCROLLABLE, 0);
        CHECK_ERROR(handle, "SQLSetStmtAttr (SQL_ATTR_CURSOR_SCROLLABLE)",
                    rc, SQL_HANDLE_STMT, statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        for (i = argp = 0; i < statement->nargs && SQL_SUCCEEDED(rc); i++) {
            rc = odbc_bind_param(pool, statement, i + 1, statement->types[i],
                                 &argp, args, BINARYMODE);
        }
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = SQLExecute(statement->stmt);
        CHECK_ERROR(handle, "SQLExecute", rc, SQL_HANDLE_STMT,
                    statement->stmt);
    }
    if (SQL_SUCCEEDED(rc)) {
        rc = odbc_create_results(handle, statement->stmt, pool, random, res);
        apr_pool_cleanup_register(pool, *res,
                                  odbc_close_results, apr_pool_cleanup_null);
    }

    return APR_FROM_SQL_RESULT(rc);
}

/** pvbquery: query using a prepared statement + binary args **/
static int odbc_pvbquery(apr_pool_t *pool, apr_dbd_t *handle, int *nrows,
                         apr_dbd_prepared_t *statement, va_list args)
{
    const char **values;
    int i;

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);
    for (i = 0; i < statement->nvals; i++)
        values[i] = va_arg(args, const char *);
    return odbc_pbquery(pool, handle, nrows, statement, (const void **)values);
}

/** pvbselect: select using a prepared statement + binary args **/
static int odbc_pvbselect(apr_pool_t *pool, apr_dbd_t *handle,
                          apr_dbd_results_t **res,
                          apr_dbd_prepared_t *statement,
                          int random, va_list args)
{
    const char **values;
    int i;

    values = apr_palloc(pool, sizeof(*values) * statement->nvals);
    for (i = 0; i < statement->nvals; i++)
        values[i] = va_arg(args, const char *);
    return odbc_pbselect(pool, handle, res, statement, random, (const void **)values);
}

APU_MODULE_DECLARE_DATA const apr_dbd_driver_t ODBC_DRIVER_ENTRY = {
    ODBC_DRIVER_STRING,
    odbc_init,
    odbc_native_handle,
    odbc_open,
    odbc_check_conn,
    odbc_close,
    odbc_set_dbname,
    odbc_start_transaction,
    odbc_end_transaction,
    odbc_query,
    odbc_select,
    odbc_num_cols,
    odbc_num_tuples,
    odbc_get_row,
    odbc_get_entry,
    odbc_error,
    odbc_escape,
    odbc_prepare,
    odbc_pvquery,
    odbc_pvselect,
    odbc_pquery,
    odbc_pselect,
    odbc_get_name,
    odbc_transaction_mode_get,
    odbc_transaction_mode_set,
    "?",
    odbc_pvbquery,
    odbc_pvbselect,
    odbc_pbquery,
    odbc_pbselect,
    odbc_datum_get
};

#endif
