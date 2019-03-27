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

/* Overview of what this is and does:
 * http://www.apache.org/~niq/dbd.html
 */

#ifndef APR_DBD_H
#define APR_DBD_H

#include "apu.h"
#include "apr_pools.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file apr_dbd.h
 * @brief APR-UTIL DBD library
 */
/**
 * @defgroup APR_Util_DBD DBD routines
 * @ingroup APR_Util
 * @{
 */

/**
 * Mapping of C to SQL types, used for prepared statements.
 * @remarks
 * For apr_dbd_p[v]query/select functions, in and out parameters are always
 * const char * (i.e. regular nul terminated strings). LOB types are passed
 * with four (4) arguments: payload, length, table and column, all as const
 * char *, where table and column are reserved for future use by Oracle.
 * @remarks
 * For apr_dbd_p[v]bquery/select functions, in and out parameters are
 * described next to each enumeration constant and are generally native binary
 * types or some APR data type. LOB types are passed with four (4) arguments:
 * payload (char*), length (apr_size_t*), table (char*) and column (char*).
 * Table and column are reserved for future use by Oracle.
 */
typedef enum {
    APR_DBD_TYPE_NONE,
    APR_DBD_TYPE_TINY,       /**< \%hhd : in, out: char* */
    APR_DBD_TYPE_UTINY,      /**< \%hhu : in, out: unsigned char* */
    APR_DBD_TYPE_SHORT,      /**< \%hd  : in, out: short* */
    APR_DBD_TYPE_USHORT,     /**< \%hu  : in, out: unsigned short* */
    APR_DBD_TYPE_INT,        /**< \%d   : in, out: int* */
    APR_DBD_TYPE_UINT,       /**< \%u   : in, out: unsigned int* */
    APR_DBD_TYPE_LONG,       /**< \%ld  : in, out: long* */
    APR_DBD_TYPE_ULONG,      /**< \%lu  : in, out: unsigned long* */
    APR_DBD_TYPE_LONGLONG,   /**< \%lld : in, out: apr_int64_t* */
    APR_DBD_TYPE_ULONGLONG,  /**< \%llu : in, out: apr_uint64_t* */
    APR_DBD_TYPE_FLOAT,      /**< \%f   : in, out: float* */
    APR_DBD_TYPE_DOUBLE,     /**< \%lf  : in, out: double* */
    APR_DBD_TYPE_STRING,     /**< \%s   : in: char*, out: char** */
    APR_DBD_TYPE_TEXT,       /**< \%pDt : in: char*, out: char** */
    APR_DBD_TYPE_TIME,       /**< \%pDi : in: char*, out: char** */
    APR_DBD_TYPE_DATE,       /**< \%pDd : in: char*, out: char** */
    APR_DBD_TYPE_DATETIME,   /**< \%pDa : in: char*, out: char** */
    APR_DBD_TYPE_TIMESTAMP,  /**< \%pDs : in: char*, out: char** */
    APR_DBD_TYPE_ZTIMESTAMP, /**< \%pDz : in: char*, out: char** */
    APR_DBD_TYPE_BLOB,       /**< \%pDb : in: char* apr_size_t* char* char*, out: apr_bucket_brigade* */
    APR_DBD_TYPE_CLOB,       /**< \%pDc : in: char* apr_size_t* char* char*, out: apr_bucket_brigade* */
    APR_DBD_TYPE_NULL        /**< \%pDn : in: void*, out: void** */
} apr_dbd_type_e;

/* These are opaque structs.  Instantiation is up to each backend */
typedef struct apr_dbd_driver_t apr_dbd_driver_t;
typedef struct apr_dbd_t apr_dbd_t;
typedef struct apr_dbd_transaction_t apr_dbd_transaction_t;
typedef struct apr_dbd_results_t apr_dbd_results_t;
typedef struct apr_dbd_row_t apr_dbd_row_t;
typedef struct apr_dbd_prepared_t apr_dbd_prepared_t;

/** apr_dbd_init: perform once-only initialisation.  Call once only.
 *
 *  @param pool - pool to register any shutdown cleanups, etc
 */
APU_DECLARE(apr_status_t) apr_dbd_init(apr_pool_t *pool);

/** apr_dbd_get_driver: get the driver struct for a name
 *
 *  @param pool - (process) pool to register cleanup
 *  @param name - driver name
 *  @param driver - pointer to driver struct.
 *  @return APR_SUCCESS for success
 *  @return APR_ENOTIMPL for no driver (when DSO not enabled)
 *  @return APR_EDSOOPEN if DSO driver file can't be opened
 *  @return APR_ESYMNOTFOUND if the driver file doesn't contain a driver
 */
APU_DECLARE(apr_status_t) apr_dbd_get_driver(apr_pool_t *pool, const char *name,
                                             const apr_dbd_driver_t **driver);

/** apr_dbd_open_ex: open a connection to a backend
 *
 *  @param driver - driver struct.
 *  @param pool - working pool
 *  @param params - arguments to driver (implementation-dependent)
 *  @param handle - pointer to handle to return
 *  @param error - descriptive error.
 *  @return APR_SUCCESS for success
 *  @return APR_EGENERAL if driver exists but connection failed
 *  @remarks PostgreSQL: the params is passed directly to the PQconnectdb()
 *  function (check PostgreSQL documentation for more details on the syntax).
 *  @remarks SQLite2: the params is split on a colon, with the first part used
 *  as the filename and second part converted to an integer and used as file
 *  mode.
 *  @remarks SQLite3: the params is passed directly to the sqlite3_open()
 *  function as a filename to be opened (check SQLite3 documentation for more
 *  details).
 *  @remarks Oracle: the params can have "user", "pass", "dbname" and "server"
 *  keys, each followed by an equal sign and a value. Such key/value pairs can
 *  be delimited by space, CR, LF, tab, semicolon, vertical bar or comma.
 *  @remarks MySQL: the params can have "host", "port", "user", "pass",
 *  "dbname", "sock", "flags" "fldsz", "group" and "reconnect" keys, each
 *  followed by an equal sign and a value. Such key/value pairs can be
 *  delimited by space, CR, LF, tab, semicolon, vertical bar or comma. For
 *  now, "flags" can only recognise CLIENT_FOUND_ROWS (check MySQL manual for
 *  details). The value associated with "fldsz" determines maximum amount of
 *  memory (in bytes) for each of the fields in the result set of prepared
 *  statements. By default, this value is 1 MB. The value associated with
 *  "group" determines which group from configuration file to use (see
 *  MYSQL_READ_DEFAULT_GROUP option of mysql_options() in MySQL manual).
 *  Reconnect is set to 1 by default (i.e. true).
 *  @remarks FreeTDS: the params can have "username", "password", "appname",
 *  "dbname", "host", "charset", "lang" and "server" keys, each followed by an
 *  equal sign and a value.
 */
APU_DECLARE(apr_status_t) apr_dbd_open_ex(const apr_dbd_driver_t *driver,
                                          apr_pool_t *pool, const char *params,
                                          apr_dbd_t **handle,
                                          const char **error);

/** apr_dbd_open: open a connection to a backend
 *
 *  @param driver - driver struct.
 *  @param pool - working pool
 *  @param params - arguments to driver (implementation-dependent)
 *  @param handle - pointer to handle to return
 *  @return APR_SUCCESS for success
 *  @return APR_EGENERAL if driver exists but connection failed
 *  @see apr_dbd_open_ex
 */
APU_DECLARE(apr_status_t) apr_dbd_open(const apr_dbd_driver_t *driver,
                                       apr_pool_t *pool, const char *params,
                                       apr_dbd_t **handle);

/** apr_dbd_close: close a connection to a backend
 *
 *  @param driver - driver struct.
 *  @param handle - handle to close
 *  @return APR_SUCCESS for success or error status
 */
APU_DECLARE(apr_status_t) apr_dbd_close(const apr_dbd_driver_t *driver,
                                        apr_dbd_t *handle);

/* apr-function-shaped versions of things */

/** apr_dbd_name: get the name of the driver
 *
 *  @param driver - the driver
 *  @return - name
 */
APU_DECLARE(const char*) apr_dbd_name(const apr_dbd_driver_t *driver);

/** apr_dbd_native_handle: get native database handle of the underlying db
 *
 *  @param driver - the driver
 *  @param handle - apr_dbd handle
 *  @return - native handle
 */
APU_DECLARE(void*) apr_dbd_native_handle(const apr_dbd_driver_t *driver,
                                         apr_dbd_t *handle);

/** check_conn: check status of a database connection
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection to check
 *  @return APR_SUCCESS or error
 */
APU_DECLARE(int) apr_dbd_check_conn(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                    apr_dbd_t *handle);

/** apr_dbd_set_dbname: select database name.  May be a no-op if not supported.
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param name - the database to select
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_set_dbname(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                    apr_dbd_t *handle, const char *name);

/** apr_dbd_transaction_start: start a transaction.  May be a no-op.
 *
 *  @param driver - the driver
 *  @param pool - a pool to use for error messages (if any).
 *  @param handle - the db connection
 *  @param trans - ptr to a transaction.  May be null on entry
 *  @return 0 for success or error code
 *  @remarks Note that transaction modes, set by calling
 *  apr_dbd_transaction_mode_set(), will affect all query/select calls within
 *  a transaction. By default, any error in query/select during a transaction
 *  will cause the transaction to inherit the error code and any further
 *  query/select calls will fail immediately. Put transaction in "ignore
 *  errors" mode to avoid that. Use "rollback" mode to do explicit rollback.
 */
APU_DECLARE(int) apr_dbd_transaction_start(const apr_dbd_driver_t *driver,
                                           apr_pool_t *pool,
                                           apr_dbd_t *handle,
                                           apr_dbd_transaction_t **trans);

/** apr_dbd_transaction_end: end a transaction
 *  (commit on success, rollback on error).
 *  May be a no-op.
 *
 *  @param driver - the driver
 *  @param handle - the db connection
 *  @param trans - the transaction.
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_transaction_end(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool,
                                         apr_dbd_transaction_t *trans);

#define APR_DBD_TRANSACTION_COMMIT        0x00  /**< commit the transaction */
#define APR_DBD_TRANSACTION_ROLLBACK      0x01  /**< rollback the transaction */
#define APR_DBD_TRANSACTION_IGNORE_ERRORS 0x02  /**< ignore transaction errors */

/** apr_dbd_transaction_mode_get: get the mode of transaction
 *
 *  @param driver - the driver
 *  @param trans  - the transaction
 *  @return mode of transaction
 */
APU_DECLARE(int) apr_dbd_transaction_mode_get(const apr_dbd_driver_t *driver,
                                              apr_dbd_transaction_t *trans);

/** apr_dbd_transaction_mode_set: set the mode of transaction
 *
 *  @param driver - the driver
 *  @param trans  - the transaction
 *  @param mode   - new mode of the transaction
 *  @return the mode of transaction in force after the call
 */
APU_DECLARE(int) apr_dbd_transaction_mode_set(const apr_dbd_driver_t *driver,
                                              apr_dbd_transaction_t *trans,
                                              int mode);

/** apr_dbd_query: execute an SQL query that doesn't return a result set
 *
 *  @param driver - the driver
 *  @param handle - the connection
 *  @param nrows - number of rows affected.
 *  @param statement - the SQL statement to execute
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_query(const apr_dbd_driver_t *driver, apr_dbd_t *handle,
                               int *nrows, const char *statement);

/** apr_dbd_select: execute an SQL query that returns a result set
 *
 *  @param driver - the driver
 *  @param pool - pool to allocate the result set
 *  @param handle - the connection
 *  @param res - pointer to result set pointer.  May point to NULL on entry
 *  @param statement - the SQL statement to execute
 *  @param random - 1 to support random access to results (seek any row);
 *                  0 to support only looping through results in order
 *                    (async access - faster)
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_select(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                apr_dbd_t *handle, apr_dbd_results_t **res,
                                const char *statement, int random);

/** apr_dbd_num_cols: get the number of columns in a results set
 *
 *  @param driver - the driver
 *  @param res - result set.
 *  @return number of columns
 */
APU_DECLARE(int) apr_dbd_num_cols(const apr_dbd_driver_t *driver,
                                  apr_dbd_results_t *res);

/** apr_dbd_num_tuples: get the number of rows in a results set
 *  of a synchronous select
 *
 *  @param driver - the driver
 *  @param res - result set.
 *  @return number of rows, or -1 if the results are asynchronous
 */
APU_DECLARE(int) apr_dbd_num_tuples(const apr_dbd_driver_t *driver,
                                    apr_dbd_results_t *res);

/** apr_dbd_get_row: get a row from a result set
 *
 *  @param driver - the driver
 *  @param pool - pool to allocate the row
 *  @param res - result set pointer
 *  @param row - pointer to row pointer.  May point to NULL on entry
 *  @param rownum - row number (counting from 1), or -1 for "next row".
 *                  Ignored if random access is not supported.
 *  @return 0 for success, -1 for rownum out of range or data finished
 */
APU_DECLARE(int) apr_dbd_get_row(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                 apr_dbd_results_t *res, apr_dbd_row_t **row,
                                 int rownum);

/** apr_dbd_get_entry: get an entry from a row
 *
 *  @param driver - the driver
 *  @param row - row pointer
 *  @param col - entry number
 *  @return value from the row, or NULL if col is out of bounds.
 */
APU_DECLARE(const char*) apr_dbd_get_entry(const apr_dbd_driver_t *driver,
                                           apr_dbd_row_t *row, int col);

/** apr_dbd_get_name: get an entry name from a result set
 *
 *  @param driver - the driver
 *  @param res - result set pointer
 *  @param col - entry number
 *  @return name of the entry, or NULL if col is out of bounds.
 */
APU_DECLARE(const char*) apr_dbd_get_name(const apr_dbd_driver_t *driver,
                                          apr_dbd_results_t *res, int col);


/** apr_dbd_error: get current error message (if any)
 *
 *  @param driver - the driver
 *  @param handle - the connection
 *  @param errnum - error code from operation that returned an error
 *  @return the database current error message, or message for errnum
 *          (implementation-dependent whether errnum is ignored)
 */
APU_DECLARE(const char*) apr_dbd_error(const apr_dbd_driver_t *driver,
                                       apr_dbd_t *handle, int errnum);

/** apr_dbd_escape: escape a string so it is safe for use in query/select
 *
 *  @param driver - the driver
 *  @param pool - pool to alloc the result from
 *  @param string - the string to escape
 *  @param handle - the connection
 *  @return the escaped, safe string
 */
APU_DECLARE(const char*) apr_dbd_escape(const apr_dbd_driver_t *driver,
                                        apr_pool_t *pool, const char *string,
                                        apr_dbd_t *handle);

/** apr_dbd_prepare: prepare a statement
 *
 *  @param driver - the driver
 *  @param pool - pool to alloc the result from
 *  @param handle - the connection
 *  @param query - the SQL query
 *  @param label - A label for the prepared statement.
 *                 use NULL for temporary prepared statements
 *                 (eg within a Request in httpd)
 *  @param statement - statement to prepare.  May point to null on entry.
 *  @return 0 for success or error code
 *  @remarks To specify parameters of the prepared query, use \%s, \%d etc.
 *  (see below for full list) in place of database specific parameter syntax
 *  (e.g.  for PostgreSQL, this would be $1, $2, for SQLite3 this would be ?
 *  etc.).  For instance: "SELECT name FROM customers WHERE name=%s" would be
 *  a query that this function understands.
 *  @remarks Here is the full list of format specifiers that this function
 *  understands and what they map to in SQL: \%hhd (TINY INT), \%hhu (UNSIGNED
 *  TINY INT), \%hd (SHORT), \%hu (UNSIGNED SHORT), \%d (INT), \%u (UNSIGNED
 *  INT), \%ld (LONG), \%lu (UNSIGNED LONG), \%lld (LONG LONG), \%llu
 *  (UNSIGNED LONG LONG), \%f (FLOAT, REAL), \%lf (DOUBLE PRECISION), \%s
 *  (VARCHAR), \%pDt (TEXT), \%pDi (TIME), \%pDd (DATE), \%pDa (DATETIME),
 *  \%pDs (TIMESTAMP), \%pDz (TIMESTAMP WITH TIME ZONE), \%pDb (BLOB), \%pDc
 *  (CLOB) and \%pDn (NULL). Not all databases have support for all these
 *  types, so the underlying driver will attempt the "best match" where
 *  possible. A \% followed by any letter not in the above list will be
 *  interpreted as VARCHAR (i.e. \%s).
 */
APU_DECLARE(int) apr_dbd_prepare(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                 apr_dbd_t *handle, const char *query,
                                 const char *label,
                                 apr_dbd_prepared_t **statement);


/** apr_dbd_pquery: query using a prepared statement + args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param nrows - number of rows affected.
 *  @param statement - the prepared statement to execute
 *  @param nargs - ignored (for backward compatibility only)
 *  @param args - args to prepared statement
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_pquery(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                apr_dbd_t *handle, int *nrows,
                                apr_dbd_prepared_t *statement, int nargs,
                                const char **args);

/** apr_dbd_pselect: select using a prepared statement + args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param res - pointer to query results.  May point to NULL on entry
 *  @param statement - the prepared statement to execute
 *  @param random - Whether to support random-access to results
 *  @param nargs - ignored (for backward compatibility only)
 *  @param args - args to prepared statement
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_pselect(const apr_dbd_driver_t *driver, apr_pool_t *pool,
                                 apr_dbd_t *handle, apr_dbd_results_t **res,
                                 apr_dbd_prepared_t *statement, int random,
                                 int nargs, const char **args);

/** apr_dbd_pvquery: query using a prepared statement + args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param nrows - number of rows affected.
 *  @param statement - the prepared statement to execute
 *  @param ... - varargs list
 *  @return 0 for success or error code
 */
APU_DECLARE_NONSTD(int) apr_dbd_pvquery(const apr_dbd_driver_t *driver, 
                                        apr_pool_t *pool,
                                        apr_dbd_t *handle, int *nrows,
                                        apr_dbd_prepared_t *statement, ...);

/** apr_dbd_pvselect: select using a prepared statement + args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param res - pointer to query results.  May point to NULL on entry
 *  @param statement - the prepared statement to execute
 *  @param random - Whether to support random-access to results
 *  @param ... - varargs list
 *  @return 0 for success or error code
 */
APU_DECLARE_NONSTD(int) apr_dbd_pvselect(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool, apr_dbd_t *handle,
                                         apr_dbd_results_t **res,
                                         apr_dbd_prepared_t *statement,
                                         int random, ...);

/** apr_dbd_pbquery: query using a prepared statement + binary args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param nrows - number of rows affected.
 *  @param statement - the prepared statement to execute
 *  @param args - binary args to prepared statement
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_pbquery(const apr_dbd_driver_t *driver,
                                 apr_pool_t *pool, apr_dbd_t *handle,
                                 int *nrows, apr_dbd_prepared_t *statement,
                                 const void **args);

/** apr_dbd_pbselect: select using a prepared statement + binary args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param res - pointer to query results.  May point to NULL on entry
 *  @param statement - the prepared statement to execute
 *  @param random - Whether to support random-access to results
 *  @param args - binary args to prepared statement
 *  @return 0 for success or error code
 */
APU_DECLARE(int) apr_dbd_pbselect(const apr_dbd_driver_t *driver,
                                  apr_pool_t *pool,
                                  apr_dbd_t *handle, apr_dbd_results_t **res,
                                  apr_dbd_prepared_t *statement, int random,
                                  const void **args);

/** apr_dbd_pvbquery: query using a prepared statement + binary args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param nrows - number of rows affected.
 *  @param statement - the prepared statement to execute
 *  @param ... - varargs list of binary args
 *  @return 0 for success or error code
 */
APU_DECLARE_NONSTD(int) apr_dbd_pvbquery(const apr_dbd_driver_t *driver,
                                         apr_pool_t *pool,
                                         apr_dbd_t *handle, int *nrows,
                                         apr_dbd_prepared_t *statement, ...);

/** apr_dbd_pvbselect: select using a prepared statement + binary args
 *
 *  @param driver - the driver
 *  @param pool - working pool
 *  @param handle - the connection
 *  @param res - pointer to query results.  May point to NULL on entry
 *  @param statement - the prepared statement to execute
 *  @param random - Whether to support random-access to results
 *  @param ... - varargs list of binary args
 *  @return 0 for success or error code
 */
APU_DECLARE_NONSTD(int) apr_dbd_pvbselect(const apr_dbd_driver_t *driver,
                                          apr_pool_t *pool, apr_dbd_t *handle,
                                          apr_dbd_results_t **res,
                                          apr_dbd_prepared_t *statement,
                                          int random, ...);

/** apr_dbd_datum_get: get a binary entry from a row
 *
 *  @param driver - the driver
 *  @param row - row pointer
 *  @param col - entry number
 *  @param type - type of data to get
 *  @param data - pointer to data, allocated by the caller
 *  @return APR_SUCCESS on success, APR_ENOENT if data is NULL or APR_EGENERAL
 */
APU_DECLARE(apr_status_t) apr_dbd_datum_get(const apr_dbd_driver_t *driver,
                                            apr_dbd_row_t *row, int col,
                                            apr_dbd_type_e type, void *data);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
