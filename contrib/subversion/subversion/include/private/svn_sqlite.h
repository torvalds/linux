/* svn_sqlite.h
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#ifndef SVN_SQLITE_H
#define SVN_SQLITE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_checksum.h"
#include "svn_error.h"

#include "private/svn_token.h"  /* for svn_token_map_t  */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Because the SQLite code can be inlined into libsvn_subre/sqlite.c,
   we define accessors to its compile-time and run-time version
   numbers here. */

/* Return the value that SQLITE_VERSION had at compile time. */
const char *svn_sqlite__compiled_version(void);

/* Return the value of sqlite3_libversion() at run time. */
const char *svn_sqlite__runtime_version(void);


typedef struct svn_sqlite__db_t svn_sqlite__db_t;
typedef struct svn_sqlite__stmt_t svn_sqlite__stmt_t;
typedef struct svn_sqlite__context_t svn_sqlite__context_t;
typedef struct svn_sqlite__value_t svn_sqlite__value_t;

typedef enum svn_sqlite__mode_e {
    svn_sqlite__mode_readonly,   /* open the database read-only */
    svn_sqlite__mode_readwrite,  /* open the database read-write */
    svn_sqlite__mode_rwcreate    /* open/create the database read-write */
} svn_sqlite__mode_t;

/* The type used for callback functions. */
typedef svn_error_t *(*svn_sqlite__func_t)(svn_sqlite__context_t *sctx,
                                           int argc,
                                           svn_sqlite__value_t *values[],
                                           void *baton);


/* Step the given statement; if it returns SQLITE_DONE, reset the statement.
   Otherwise, raise an SVN error.  */
svn_error_t *
svn_sqlite__step_done(svn_sqlite__stmt_t *stmt);

/* Step the given statement; raise an SVN error (and reset the
   statement) if it doesn't return SQLITE_ROW. */
svn_error_t *
svn_sqlite__step_row(svn_sqlite__stmt_t *stmt);

/* Step the given statement; raise an SVN error (and reset the
   statement) if it doesn't return SQLITE_DONE or SQLITE_ROW.  Set
   *GOT_ROW to true iff it got SQLITE_ROW.
*/
svn_error_t *
svn_sqlite__step(svn_boolean_t *got_row, svn_sqlite__stmt_t *stmt);

/* Perform an insert as given by the prepared and bound STMT, and set
   *ROW_ID to the id of the inserted row if ROW_ID is non-NULL.
   STMT will be reset prior to returning. */
svn_error_t *
svn_sqlite__insert(apr_int64_t *row_id, svn_sqlite__stmt_t *stmt);

/* Perform an update/delete and then return the number of affected rows.
   If AFFECTED_ROWS is not NULL, then set *AFFECTED_ROWS to the
   number of rows changed.
   STMT will be reset prior to returning. */
svn_error_t *
svn_sqlite__update(int *affected_rows, svn_sqlite__stmt_t *stmt);

/* Return in *VERSION the version of the schema in DB. Use SCRATCH_POOL
   for temporary allocations.  */
svn_error_t *
svn_sqlite__read_schema_version(int *version,
                                svn_sqlite__db_t *db,
                                apr_pool_t *scratch_pool);



/* Open a connection in *DB to the database at PATH. Validate the schema,
   creating/upgrading to LATEST_SCHEMA if needed using the instructions
   in UPGRADE_SQL. The resulting DB is allocated in RESULT_POOL, and any
   temporary allocations are made in SCRATCH_POOL.

   STATEMENTS is an array of strings which may eventually be executed, the
   last element of which should be NULL.  These strings and the array itself
   are not duplicated internally, and should have a lifetime at least as long
   as RESULT_POOL.
   STATEMENTS itself may be NULL, in which case it has no impact.
   See svn_sqlite__get_statement() for how these strings are used.

   TIMEOUT defines the SQLite busy timeout, values <= 0 cause a Subversion
   default to be used.

   The statements will be finalized and the SQLite database will be closed
   when RESULT_POOL is cleaned up. */
svn_error_t *
svn_sqlite__open(svn_sqlite__db_t **db, const char *path,
                 svn_sqlite__mode_t mode, const char * const statements[],
                 int latest_schema, const char * const *upgrade_sql,
                 apr_int32_t timeout,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool);

/* Explicitly close the connection in DB. */
svn_error_t *
svn_sqlite__close(svn_sqlite__db_t *db);

/* Add a custom function to be used with this database connection.  The data
   in BATON should live at least as long as the connection in DB.

   Pass TRUE if the result of the function is constant within a statement with
   a specific set of argument values and FALSE if not (or when in doubt). When
   TRUE newer Sqlite versions use this knowledge for query optimizations. */
svn_error_t *
svn_sqlite__create_scalar_function(svn_sqlite__db_t *db,
                                   const char *func_name,
                                   int argc,
                                   svn_boolean_t deterministic,
                                   svn_sqlite__func_t func,
                                   void *baton);

/* Execute the (multiple) statements in the STATEMENTS[STMT_IDX] string.  */
svn_error_t *
svn_sqlite__exec_statements(svn_sqlite__db_t *db, int stmt_idx);

/* Return the statement in *STMT which has been prepared from the
   STATEMENTS[STMT_IDX] string, where STATEMENTS is the array that was
   passed to svn_sqlite__open().  This statement is allocated in the same
   pool as the DB, and will be cleaned up when DB is closed. */
svn_error_t *
svn_sqlite__get_statement(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                          int stmt_idx);


/* ---------------------------------------------------------------------

   BINDING VALUES

*/

/* Bind values to SQL parameters in STMT, according to FMT.  FMT may contain:

   Spec  Argument type             Item type
   ----  -----------------         ---------
   n     <none, absent>            Column assignment skip
   d     int                       Number
   L     apr_int64_t               Number
   i     apr_int64_t               Number (deprecated format spec)
   s     const char *              String
   b     const void *              Blob data
         apr_size_t                Blob length
   r     svn_revnum_t              Revision number
   t     const svn_token_map_t *   Token mapping table
         int                       Token value

  Each character in FMT maps to one SQL parameter, and one or two function
  parameters, in the order they appear.
*/
svn_error_t *
svn_sqlite__bindf(svn_sqlite__stmt_t *stmt, const char *fmt, ...);

/* Error-handling wrapper around sqlite3_bind_int. */
svn_error_t *
svn_sqlite__bind_int(svn_sqlite__stmt_t *stmt, int slot, int val);

/* Error-handling wrapper around sqlite3_bind_int64. */
svn_error_t *
svn_sqlite__bind_int64(svn_sqlite__stmt_t *stmt, int slot,
                       apr_int64_t val);

/* Error-handling wrapper around sqlite3_bind_text. VAL cannot contain
   zero bytes; we always pass SQLITE_TRANSIENT. */
svn_error_t *
svn_sqlite__bind_text(svn_sqlite__stmt_t *stmt, int slot,
                      const char *val);

/* Error-handling wrapper around sqlite3_bind_blob. */
svn_error_t *
svn_sqlite__bind_blob(svn_sqlite__stmt_t *stmt,
                      int slot,
                      const void *val,
                      apr_size_t len);

/* Look up VALUE in MAP, and bind the resulting token word at SLOT.  */
svn_error_t *
svn_sqlite__bind_token(svn_sqlite__stmt_t *stmt,
                       int slot,
                       const svn_token_map_t *map,
                       int value);

/* Bind the value to SLOT, unless SVN_IS_VALID_REVNUM(value) is false,
   in which case it binds NULL.  */
svn_error_t *
svn_sqlite__bind_revnum(svn_sqlite__stmt_t *stmt, int slot,
                        svn_revnum_t value);

/* Bind a set of properties to the given slot. If PROPS is NULL, then no
   binding will occur. PROPS will be stored as a serialized skel. */
svn_error_t *
svn_sqlite__bind_properties(svn_sqlite__stmt_t *stmt,
                            int slot,
                            const apr_hash_t *props,
                            apr_pool_t *scratch_pool);

/* Bind a set of inherited properties to the given slot. If INHERITED_PROPS
   is NULL, then no binding will occur. INHERITED_PROPS will be stored as a
   serialized skel. */
svn_error_t *
svn_sqlite__bind_iprops(svn_sqlite__stmt_t *stmt,
                        int slot,
                        const apr_array_header_t *inherited_props,
                        apr_pool_t *scratch_pool);

/* Bind a checksum's value to the given slot. If CHECKSUM is NULL, then no
   binding will occur. */
svn_error_t *
svn_sqlite__bind_checksum(svn_sqlite__stmt_t *stmt,
                          int slot,
                          const svn_checksum_t *checksum,
                          apr_pool_t *scratch_pool);


/* ---------------------------------------------------------------------

   FETCHING VALUES

*/

/* Wrapper around sqlite3_column_blob and sqlite3_column_bytes. The return
   value will be NULL if the column is null.

   If RESULT_POOL is not NULL, allocate the return value (if any) in it.
   If RESULT_POOL is NULL, the return value will be valid until an
   invocation of svn_sqlite__column_* performs a data type conversion (as
   described in the SQLite documentation) or the statement is stepped or
   reset or finalized. */
const void *
svn_sqlite__column_blob(svn_sqlite__stmt_t *stmt, int column,
                        apr_size_t *len, apr_pool_t *result_pool);

/* Wrapper around sqlite3_column_text. If the column is null, then the
   return value will be NULL.

   If RESULT_POOL is not NULL, allocate the return value (if any) in it.
   If RESULT_POOL is NULL, the return value will be valid until an
   invocation of svn_sqlite__column_* performs a data type conversion (as
   described in the SQLite documentation) or the statement is stepped or
   reset or finalized. */
const char *
svn_sqlite__column_text(svn_sqlite__stmt_t *stmt, int column,
                        apr_pool_t *result_pool);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be SVN_INVALID_REVNUM. */
svn_revnum_t
svn_sqlite__column_revnum(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be FALSE. */
svn_boolean_t
svn_sqlite__column_boolean(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int. If the column is null, then the
   return value will be 0. */
int
svn_sqlite__column_int(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be 0. */
apr_int64_t
svn_sqlite__column_int64(svn_sqlite__stmt_t *stmt, int column);

/* Fetch the word at COLUMN, look it up in the MAP, and return its value.
   MALFUNCTION is thrown if the column is null or contains an unknown word.  */
int
svn_sqlite__column_token(svn_sqlite__stmt_t *stmt,
                         int column,
                         const svn_token_map_t *map);

/* Fetch the word at COLUMN, look it up in the MAP, and return its value.
   Returns NULL_VAL if the column is null. MALFUNCTION is thrown if the
   column contains an unknown word.  */
int
svn_sqlite__column_token_null(svn_sqlite__stmt_t *stmt,
                              int column,
                              const svn_token_map_t *map,
                              int null_val);

/* Return the column as a hash of const char * => const svn_string_t *.
   If the column is null, then set *PROPS to NULL. The
   results will be allocated in RESULT_POOL, and any temporary allocations
   will be made in SCRATCH_POOL. */
svn_error_t *
svn_sqlite__column_properties(apr_hash_t **props,
                              svn_sqlite__stmt_t *stmt,
                              int column,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Return the column as an array of depth-first ordered array of
   svn_prop_inherited_item_t * structures.  If the column is null, then
   set *IPROPS to NULL. The results will be allocated in RESULT_POOL,
   and any temporary allocations will be made in SCRATCH_POOL. */
svn_error_t *
svn_sqlite__column_iprops(apr_array_header_t **iprops,
                          svn_sqlite__stmt_t *stmt,
                          int column,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Return the column as a checksum. If the column is null, then NULL will
   be stored into *CHECKSUM. The result will be allocated in RESULT_POOL. */
svn_error_t *
svn_sqlite__column_checksum(const svn_checksum_t **checksum,
                            svn_sqlite__stmt_t *stmt,
                            int column,
                            apr_pool_t *result_pool);

/* Return TRUE if the result of selecting the column is null,
   FALSE otherwise */
svn_boolean_t
svn_sqlite__column_is_null(svn_sqlite__stmt_t *stmt, int column);

/* Return the number of bytes the column uses in a text or blob representation.
   0 for NULL columns. */
int
svn_sqlite__column_bytes(svn_sqlite__stmt_t *stmt, int column);

/* When Subversion is compiled in maintainer mode: enables the sqlite error
   logging to SVN_DBG_OUTPUT. */
void
svn_sqlite__dbg_enable_errorlog(void);


/* --------------------------------------------------------------------- */

#define SVN_SQLITE__INTEGER  1
#define SVN_SQLITE__FLOAT    2
#define SVN_SQLITE__TEXT     3
#define SVN_SQLITE__BLOB     4
#define SVN_SQLITE__NULL     5

/* */
int
svn_sqlite__value_type(svn_sqlite__value_t *val);

/* */
const char *
svn_sqlite__value_text(svn_sqlite__value_t *val);


/* --------------------------------------------------------------------- */

/* */
void
svn_sqlite__result_null(svn_sqlite__context_t *sctx);

void
svn_sqlite__result_int64(svn_sqlite__context_t *sctx, apr_int64_t val);

void
svn_sqlite__result_error(svn_sqlite__context_t *sctx, const char *msg, int num);


/* --------------------------------------------------------------------- */


/* Error-handling wrapper around sqlite3_finalize. */
svn_error_t *
svn_sqlite__finalize(svn_sqlite__stmt_t *stmt);

/* Reset STMT by calling sqlite3_reset(), and also clear any bindings to it.

   Note: svn_sqlite__get_statement() calls this function automatically if
   the requested statement has been used and has not yet been reset. */
svn_error_t *
svn_sqlite__reset(svn_sqlite__stmt_t *stmt);


/* Begin a transaction in DB. */
svn_error_t *
svn_sqlite__begin_transaction(svn_sqlite__db_t *db);

/* Like svn_sqlite__begin_transaction(), but takes out a 'RESERVED' lock
   immediately, instead of using the default deferred locking scheme. */
svn_error_t *
svn_sqlite__begin_immediate_transaction(svn_sqlite__db_t *db);

/* Begin a savepoint in DB. */
svn_error_t *
svn_sqlite__begin_savepoint(svn_sqlite__db_t *db);

/* Commit the current transaction in DB if ERR is SVN_NO_ERROR, otherwise
 * roll back the transaction.  Return a composition of ERR and any error
 * that may occur during the commit or roll-back. */
svn_error_t *
svn_sqlite__finish_transaction(svn_sqlite__db_t *db,
                               svn_error_t *err);

/* Release the current savepoint in DB if EXPR is SVN_NO_ERROR, otherwise
 * roll back to the savepoint and then release it.  Return a composition of
 * ERR and any error that may occur during the release or roll-back. */
svn_error_t *
svn_sqlite__finish_savepoint(svn_sqlite__db_t *db,
                             svn_error_t *err);

/* Evaluate the expression EXPR within a transaction.
 *
 * Begin a transaction in DB; evaluate the expression EXPR, which would
 * typically be a function call that does some work in DB; finally commit
 * the transaction if EXPR evaluated to SVN_NO_ERROR, otherwise roll back
 * the transaction.
 */
#define SVN_SQLITE__WITH_TXN(expr, db)                                        \
  do {                                                                        \
    svn_sqlite__db_t *svn_sqlite__db = (db);                                  \
    svn_error_t *svn_sqlite__err;                                             \
                                                                              \
    SVN_ERR(svn_sqlite__begin_transaction(svn_sqlite__db));                   \
    svn_sqlite__err = (expr);                                                 \
    SVN_ERR(svn_sqlite__finish_transaction(svn_sqlite__db, svn_sqlite__err)); \
  } while (0)

/* Callback function to for use with svn_sqlite__with_transaction(). */
typedef svn_error_t *(*svn_sqlite__transaction_callback_t)(
  void *baton, svn_sqlite__db_t *db, apr_pool_t *scratch_pool);

/* Helper function to handle SQLite transactions.  All the work done inside
   CB_FUNC will be wrapped in an SQLite transaction, which will be committed
   if CB_FUNC does not return an error.  If any error is returned from CB_FUNC,
   the transaction will be rolled back.  DB and CB_BATON will be passed to
   CB_FUNC. SCRATCH_POOL will be passed to the callback (NULL is valid). */
svn_error_t *
svn_sqlite__with_transaction(svn_sqlite__db_t *db,
                             svn_sqlite__transaction_callback_t cb_func,
                             void *cb_baton, apr_pool_t *scratch_pool);

/* Like SVN_SQLITE__WITH_TXN(), but takes out a 'RESERVED' lock
   immediately, instead of using the default deferred locking scheme. */
#define SVN_SQLITE__WITH_IMMEDIATE_TXN(expr, db)                              \
  do {                                                                        \
    svn_sqlite__db_t *svn_sqlite__db = (db);                                  \
    svn_error_t *svn_sqlite__err;                                             \
                                                                              \
    SVN_ERR(svn_sqlite__begin_immediate_transaction(svn_sqlite__db));         \
    svn_sqlite__err = (expr);                                                 \
    SVN_ERR(svn_sqlite__finish_transaction(svn_sqlite__db, svn_sqlite__err)); \
  } while (0)

/* Like svn_sqlite__with_transaction(), but takes out a 'RESERVED' lock
   immediately, instead of using the default deferred locking scheme. */
svn_error_t *
svn_sqlite__with_immediate_transaction(svn_sqlite__db_t *db,
                                       svn_sqlite__transaction_callback_t cb_func,
                                       void *cb_baton,
                                       apr_pool_t *scratch_pool);

/* Evaluate the expression EXPR within a 'savepoint'.  Savepoints can be
 * nested.
 *
 * Begin a savepoint in DB; evaluate the expression EXPR, which would
 * typically be a function call that does some work in DB; finally release
 * the savepoint if EXPR evaluated to SVN_NO_ERROR, otherwise roll back
 * to the savepoint and then release it.
 */
#define SVN_SQLITE__WITH_LOCK(expr, db)                                       \
  do {                                                                        \
    svn_sqlite__db_t *svn_sqlite__db = (db);                                  \
    svn_error_t *svn_sqlite__err;                                             \
                                                                              \
    SVN_ERR(svn_sqlite__begin_savepoint(svn_sqlite__db));                     \
    svn_sqlite__err = (expr);                                                 \
    SVN_ERR(svn_sqlite__finish_savepoint(svn_sqlite__db, svn_sqlite__err));   \
  } while (0)

/* Evaluate the expression EXPR1..EXPR4 within a 'savepoint'.  Savepoints can
 * be nested.
 *
 * Begin a savepoint in DB; evaluate the expression EXPR1, which would
 * typically be a function call that does some work in DB; if no error occurred,
 * run EXPR2; if no error occurred EXPR3; ... and finally release
 * the savepoint if EXPR evaluated to SVN_NO_ERROR, otherwise roll back
 * to the savepoint and then release it.
 */
#define SVN_SQLITE__WITH_LOCK4(expr1, expr2, expr3, expr4, db)                \
  do {                                                                        \
    svn_sqlite__db_t *svn_sqlite__db = (db);                                  \
    svn_error_t *svn_sqlite__err;                                             \
                                                                              \
    SVN_ERR(svn_sqlite__begin_savepoint(svn_sqlite__db));                     \
    svn_sqlite__err = (expr1);                                                \
    if (!svn_sqlite__err)                                                     \
      svn_sqlite__err = (expr2);                                              \
    if (!svn_sqlite__err)                                                     \
      svn_sqlite__err = (expr3);                                              \
    if (!svn_sqlite__err)                                                     \
      svn_sqlite__err = (expr4);                                              \
    SVN_ERR(svn_sqlite__finish_savepoint(svn_sqlite__db, svn_sqlite__err));   \
  } while (0)


/* Helper function to handle several SQLite operations inside a shared lock.
   This callback is similar to svn_sqlite__with_transaction(), but can be
   nested (even with a transaction).

   Using this function as a wrapper around a group of operations can give a
   *huge* performance boost as the shared-read lock will be shared over
   multiple statements, instead of being reobtained every time, which may
   require disk and/or network io, depending on SQLite's locking strategy.

   SCRATCH_POOL will be passed to the callback (NULL is valid).

   ### Since we now require SQLite >= 3.6.18, this function has the effect of
       always behaving like a deferred transaction.  Can it be combined with
       svn_sqlite__with_transaction()?
 */
svn_error_t *
svn_sqlite__with_lock(svn_sqlite__db_t *db,
                      svn_sqlite__transaction_callback_t cb_func,
                      void *cb_baton,
                      apr_pool_t *scratch_pool);


/* Hotcopy an SQLite database from SRC_PATH to DST_PATH. */
svn_error_t *
svn_sqlite__hotcopy(const char *src_path,
                    const char *dst_path,
                    apr_pool_t *scratch_pool);

/* Evaluate the expression EXPR.  If any error is returned, close
 * the connection in DB. */
#define SVN_SQLITE__ERR_CLOSE(expr, db) do                            \
{                                                                     \
  svn_error_t *svn__err = (expr);                                     \
  if (svn__err)                                                       \
    return svn_error_compose_create(svn__err, svn_sqlite__close(db)); \
} while (0)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SQLITE_H */
