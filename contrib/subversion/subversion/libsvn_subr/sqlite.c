/* sqlite.c
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_checksum.h"

#include "internal_statements.h"

#include "private/svn_sqlite.h"
#include "svn_private_config.h"
#include "private/svn_dep_compat.h"
#include "private/svn_atomic.h"
#include "private/svn_skel.h"
#include "private/svn_token.h"
#ifdef WIN32
#include "private/svn_io_private.h"
#include "private/svn_utf_private.h"
#endif

#ifdef SVN_UNICODE_NORMALIZATION_FIXES
#include "private/svn_utf_private.h"
#include "private/svn_string_private.h"
#endif /* SVN_UNICODE_NORMALIZATION_FIXES */

#ifdef SQLITE3_DEBUG
#include "private/svn_debug.h"
#endif

#ifdef SVN_SQLITE_INLINE
/* Import the sqlite3 API vtable from sqlite3wrapper.c */
#  define SQLITE_OMIT_DEPRECATED
#  include <sqlite3ext.h>
extern const sqlite3_api_routines *const svn_sqlite3__api_funcs;
extern int (*const svn_sqlite3__api_initialize)(void);
extern int (*const svn_sqlite3__api_config)(int, ...);
#  define sqlite3_api svn_sqlite3__api_funcs
#  define sqlite3_initialize svn_sqlite3__api_initialize
#  define sqlite3_config svn_sqlite3__api_config
#else
#  include <sqlite3.h>
#endif

#if !SQLITE_VERSION_AT_LEAST(3,8,2)
#error SQLite is too old -- version 3.8.2 is the minimum required version
#endif

#ifndef SQLITE_DETERMINISTIC
#define SQLITE_DETERMINISTIC 0
#endif

#ifdef SVN_UNICODE_NORMALIZATION_FIXES
/* Limit the length of a GLOB or LIKE pattern. */
#ifndef SQLITE_MAX_LIKE_PATTERN_LENGTH
# define SQLITE_MAX_LIKE_PATTERN_LENGTH 50000
#endif
#endif /* SVN_UNICODE_NORMALIZATION_FIXES */

const char *
svn_sqlite__compiled_version(void)
{
  static const char sqlite_version[] = SQLITE_VERSION;
  return sqlite_version;
}

const char *
svn_sqlite__runtime_version(void)
{
  return sqlite3_libversion();
}


INTERNAL_STATEMENTS_SQL_DECLARE_STATEMENTS(internal_statements);


#ifdef SQLITE3_DEBUG
/* An sqlite query execution callback. */
static void
sqlite_tracer(void *data, const char *sql)
{
  /*  sqlite3 *db3 = data; */
  SVN_DBG(("sql=\"%s\"\n", sql));
}
#endif

#ifdef SQLITE3_PROFILE
/* An sqlite execution timing callback. */
static void
sqlite_profiler(void *data, const char *sql, sqlite3_uint64 duration)
{
  /*  sqlite3 *db3 = data; */
  SVN_DBG(("[%.3f] sql=\"%s\"\n", 1e-9 * duration, sql));
}
#endif

#if defined(SVN_DEBUG) && defined(SQLITE_CONFIG_LOG)
static void
sqlite_error_log(void* baton, int err, const char* msg)
{
  fprintf(SVN_DBG_OUTPUT, "DBG: sqlite[S%d]: %s\n", err, msg);
}
#endif

void
svn_sqlite__dbg_enable_errorlog(void)
{
#if defined(SVN_DEBUG) && defined(SQLITE_CONFIG_LOG)
  sqlite3_config(SQLITE_CONFIG_LOG, sqlite_error_log, (void*)NULL /* baton */);
#endif
}


struct svn_sqlite__db_t
{
  sqlite3 *db3;
  const char * const *statement_strings;
  int nbr_statements;
  svn_sqlite__stmt_t **prepared_stmts;
  apr_pool_t *state_pool;

#ifdef SVN_UNICODE_NORMALIZATION_FIXES
  /* Buffers for SQLite extensoins. */
  svn_membuf_t sqlext_buf1;
  svn_membuf_t sqlext_buf2;
  svn_membuf_t sqlext_buf3;
#endif /* SVN_UNICODE_NORMALIZATION_FIXES */
};

struct svn_sqlite__stmt_t
{
  sqlite3_stmt *s3stmt;
  svn_sqlite__db_t *db;
  svn_boolean_t needs_reset;
};

struct svn_sqlite__context_t
{
  sqlite3_context *context;
};

struct svn_sqlite__value_t
{
  sqlite3_value *value;
};


/* Convert SQLite error codes to SVN. Evaluates X multiple times */
#define SQLITE_ERROR_CODE(x) ((x) == SQLITE_READONLY            \
                              ? SVN_ERR_SQLITE_READONLY         \
                              : ((x) == SQLITE_BUSY             \
                                 ? SVN_ERR_SQLITE_BUSY          \
                                 : ((x) == SQLITE_CONSTRAINT    \
                                    ? SVN_ERR_SQLITE_CONSTRAINT \
                                    : SVN_ERR_SQLITE_ERROR)))


/* SQLITE->SVN quick error wrap, much like SVN_ERR. */
#define SQLITE_ERR(x, db) do                                     \
{                                                                \
  int sqlite_err__temp = (x);                                    \
  if (sqlite_err__temp != SQLITE_OK)                             \
    return svn_error_createf(SQLITE_ERROR_CODE(sqlite_err__temp), \
                             NULL, "sqlite[S%d]: %s",             \
                             sqlite_err__temp,                    \
                             sqlite3_errmsg((db)->db3));          \
} while (0)

#define SQLITE_ERR_CLOSE(x, db, pool) do                          \
{                                                                 \
  int sqlite_err__temp = (x);                                     \
  if (sqlite_err__temp != SQLITE_OK)                              \
    {                                                             \
      const char *sqlite_err__msg                                 \
        = apr_pstrdup(pool, sqlite3_errmsg((db)->db3));           \
      return svn_error_compose_create(                            \
           svn_error_createf(SQLITE_ERROR_CODE(sqlite_err__temp), \
                             NULL, "sqlite[S%d]: %s",             \
                             sqlite_err__temp, sqlite_err__msg),  \
           svn_sqlite__close(db));                                \
    }                                                             \
} while (0)

#define SQLITE_ERR_MSG(x, msg) do                                \
{                                                                \
  int sqlite_err__temp = (x);                                    \
  if (sqlite_err__temp != SQLITE_OK)                             \
    return svn_error_createf(SQLITE_ERROR_CODE(sqlite_err__temp), \
                             NULL, "sqlite[S%d]: %s",            \
                             sqlite_err__temp, msg);             \
} while (0)


/* Time (in milliseconds) to wait for sqlite locks before giving up. */
#define BUSY_TIMEOUT 10000


/* Convenience wrapper around exec_sql2(). */
#define exec_sql(db, sql) exec_sql2((db), (sql), SQLITE_OK)

/* Run the statement SQL on DB, ignoring SQLITE_OK and IGNORED_ERR.
   (Note: the IGNORED_ERR parameter itself is not ignored.) */
static svn_error_t *
exec_sql2(svn_sqlite__db_t *db, const char *sql, int ignored_err)
{
  char *err_msg;
  int sqlite_err = sqlite3_exec(db->db3, sql, NULL, NULL, &err_msg);

  if (sqlite_err != SQLITE_OK && sqlite_err != ignored_err)
    {
      svn_error_t *err = svn_error_createf(SQLITE_ERROR_CODE(sqlite_err), NULL,
                                           _("sqlite[S%d]: %s,"
                                             " executing statement '%s'"),
                                           sqlite_err, err_msg, sql);
      sqlite3_free(err_msg);
      return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
prepare_statement(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                  const char *text, apr_pool_t *result_pool)
{
  *stmt = apr_palloc(result_pool, sizeof(**stmt));
  (*stmt)->db = db;
  (*stmt)->needs_reset = FALSE;

  SQLITE_ERR(sqlite3_prepare_v2(db->db3, text, -1, &(*stmt)->s3stmt, NULL), db);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_sqlite__exec_statements(svn_sqlite__db_t *db, int stmt_idx)
{
  SVN_ERR_ASSERT(stmt_idx < db->nbr_statements);

  return svn_error_trace(exec_sql(db, db->statement_strings[stmt_idx]));
}


svn_error_t *
svn_sqlite__get_statement(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                          int stmt_idx)
{
  SVN_ERR_ASSERT(stmt_idx < db->nbr_statements);

  if (db->prepared_stmts[stmt_idx] == NULL)
    SVN_ERR(prepare_statement(&db->prepared_stmts[stmt_idx], db,
                              db->statement_strings[stmt_idx],
                              db->state_pool));

  *stmt = db->prepared_stmts[stmt_idx];

  if ((*stmt)->needs_reset)
    return svn_error_trace(svn_sqlite__reset(*stmt));

  return SVN_NO_ERROR;
}

/* Like svn_sqlite__get_statement but gets an internal statement.

   All internal statements that use this api are executed with step_done(),
   so we don't need the fallback reset handling here or in the pool cleanup */
static svn_error_t *
get_internal_statement(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                       int stmt_idx)
{
  /* The internal statements are stored after the registered statements */
  int prep_idx = db->nbr_statements + stmt_idx;
  SVN_ERR_ASSERT(stmt_idx < STMT_INTERNAL_LAST);

  if (db->prepared_stmts[prep_idx] == NULL)
    SVN_ERR(prepare_statement(&db->prepared_stmts[prep_idx], db,
                              internal_statements[stmt_idx],
                              db->state_pool));

  *stmt = db->prepared_stmts[prep_idx];

  return SVN_NO_ERROR;
}


static svn_error_t *
step_with_expectation(svn_sqlite__stmt_t* stmt,
                      svn_boolean_t expecting_row)
{
  svn_boolean_t got_row;

  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  if ((got_row && !expecting_row)
      ||
      (!got_row && expecting_row))
    return svn_error_create(SVN_ERR_SQLITE_ERROR,
                            svn_sqlite__reset(stmt),
                            expecting_row
                              ? _("sqlite: Expected database row missing")
                              : _("sqlite: Extra database row found"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__step_done(svn_sqlite__stmt_t *stmt)
{
  SVN_ERR(step_with_expectation(stmt, FALSE));
  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_sqlite__step_row(svn_sqlite__stmt_t *stmt)
{
  return svn_error_trace(step_with_expectation(stmt, TRUE));
}


svn_error_t *
svn_sqlite__step(svn_boolean_t *got_row, svn_sqlite__stmt_t *stmt)
{
  int sqlite_result = sqlite3_step(stmt->s3stmt);

  if (sqlite_result != SQLITE_DONE && sqlite_result != SQLITE_ROW)
    {
      svn_error_t *err1, *err2;

      err1 = svn_error_createf(SQLITE_ERROR_CODE(sqlite_result), NULL,
                               "sqlite[S%d]: %s",
                               sqlite_result, sqlite3_errmsg(stmt->db->db3));
      err2 = svn_sqlite__reset(stmt);
      return svn_error_compose_create(err1, err2);
    }

  *got_row = (sqlite_result == SQLITE_ROW);
  stmt->needs_reset = TRUE;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__insert(apr_int64_t *row_id, svn_sqlite__stmt_t *stmt)
{
  svn_boolean_t got_row;

  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  if (row_id)
    *row_id = sqlite3_last_insert_rowid(stmt->db->db3);

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_sqlite__update(int *affected_rows, svn_sqlite__stmt_t *stmt)
{
  SVN_ERR(step_with_expectation(stmt, FALSE));

  if (affected_rows)
    *affected_rows = sqlite3_changes(stmt->db->db3);

  return svn_error_trace(svn_sqlite__reset(stmt));
}


static svn_error_t *
vbindf(svn_sqlite__stmt_t *stmt, const char *fmt, va_list ap)
{
  int count;

  for (count = 1; *fmt; fmt++, count++)
    {
      const void *blob;
      apr_size_t blob_size;
      const svn_token_map_t *map;

      switch (*fmt)
        {
          case 's':
            SVN_ERR(svn_sqlite__bind_text(stmt, count,
                                          va_arg(ap, const char *)));
            break;

          case 'd':
            SVN_ERR(svn_sqlite__bind_int(stmt, count,
                                         va_arg(ap, int)));
            break;

          case 'i':
          case 'L':
            SVN_ERR(svn_sqlite__bind_int64(stmt, count,
                                           va_arg(ap, apr_int64_t)));
            break;

          case 'b':
            blob = va_arg(ap, const void *);
            blob_size = va_arg(ap, apr_size_t);
            SVN_ERR(svn_sqlite__bind_blob(stmt, count, blob, blob_size));
            break;

          case 'r':
            SVN_ERR(svn_sqlite__bind_revnum(stmt, count,
                                            va_arg(ap, svn_revnum_t)));
            break;

          case 't':
            map = va_arg(ap, const svn_token_map_t *);
            SVN_ERR(svn_sqlite__bind_token(stmt, count, map, va_arg(ap, int)));
            break;

          case 'n':
            /* Skip this column: no binding */
            break;

          default:
            SVN_ERR_MALFUNCTION();
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bindf(svn_sqlite__stmt_t *stmt, const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vbindf(stmt, fmt, ap);
  va_end(ap);
  return svn_error_trace(err);
}

svn_error_t *
svn_sqlite__bind_int(svn_sqlite__stmt_t *stmt,
                     int slot,
                     int val)
{
  SQLITE_ERR(sqlite3_bind_int(stmt->s3stmt, slot, val), stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_int64(svn_sqlite__stmt_t *stmt,
                       int slot,
                       apr_int64_t val)
{
  SQLITE_ERR(sqlite3_bind_int64(stmt->s3stmt, slot, val), stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_text(svn_sqlite__stmt_t *stmt,
                      int slot,
                      const char *val)
{
  SQLITE_ERR(sqlite3_bind_text(stmt->s3stmt, slot, val, -1, SQLITE_TRANSIENT),
             stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_blob(svn_sqlite__stmt_t *stmt,
                      int slot,
                      const void *val,
                      apr_size_t len)
{
  SQLITE_ERR(sqlite3_bind_blob(stmt->s3stmt, slot, val, (int) len,
                               SQLITE_TRANSIENT),
             stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_token(svn_sqlite__stmt_t *stmt,
                       int slot,
                       const svn_token_map_t *map,
                       int value)
{
  const char *word = svn_token__to_word(map, value);

  SQLITE_ERR(sqlite3_bind_text(stmt->s3stmt, slot, word, -1, SQLITE_STATIC),
             stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_revnum(svn_sqlite__stmt_t *stmt,
                        int slot,
                        svn_revnum_t value)
{
  if (SVN_IS_VALID_REVNUM(value))
    SQLITE_ERR(sqlite3_bind_int64(stmt->s3stmt, slot,
                                  (sqlite_int64)value), stmt->db);
  else
    SQLITE_ERR(sqlite3_bind_null(stmt->s3stmt, slot), stmt->db);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__bind_properties(svn_sqlite__stmt_t *stmt,
                            int slot,
                            const apr_hash_t *props,
                            apr_pool_t *scratch_pool)
{
  svn_skel_t *skel;
  svn_stringbuf_t *properties;

  if (props == NULL)
    return svn_error_trace(svn_sqlite__bind_blob(stmt, slot, NULL, 0));

  SVN_ERR(svn_skel__unparse_proplist(&skel, props, scratch_pool));
  properties = svn_skel__unparse(skel, scratch_pool);
  return svn_error_trace(svn_sqlite__bind_blob(stmt,
                                               slot,
                                               properties->data,
                                               properties->len));
}

svn_error_t *
svn_sqlite__bind_iprops(svn_sqlite__stmt_t *stmt,
                        int slot,
                        const apr_array_header_t *inherited_props,
                        apr_pool_t *scratch_pool)
{
  svn_skel_t *skel;
  svn_stringbuf_t *properties;

  if (inherited_props == NULL)
    return svn_error_trace(svn_sqlite__bind_blob(stmt, slot, NULL, 0));

  SVN_ERR(svn_skel__unparse_iproplist(&skel, inherited_props,
                                      scratch_pool, scratch_pool));
  properties = svn_skel__unparse(skel, scratch_pool);
  return svn_error_trace(svn_sqlite__bind_blob(stmt,
                                               slot,
                                               properties->data,
                                               properties->len));
}

svn_error_t *
svn_sqlite__bind_checksum(svn_sqlite__stmt_t *stmt,
                          int slot,
                          const svn_checksum_t *checksum,
                          apr_pool_t *scratch_pool)
{
  const char *csum_str;

  if (checksum == NULL)
    csum_str = NULL;
  else
    csum_str = svn_checksum_serialize(checksum, scratch_pool, scratch_pool);

  return svn_error_trace(svn_sqlite__bind_text(stmt, slot, csum_str));
}


const void *
svn_sqlite__column_blob(svn_sqlite__stmt_t *stmt, int column,
                        apr_size_t *len, apr_pool_t *result_pool)
{
  const void *val = sqlite3_column_blob(stmt->s3stmt, column);
  *len = sqlite3_column_bytes(stmt->s3stmt, column);

  if (result_pool && val != NULL)
    val = apr_pmemdup(result_pool, val, *len);

  return val;
}

const char *
svn_sqlite__column_text(svn_sqlite__stmt_t *stmt, int column,
                        apr_pool_t *result_pool)
{
  /* cast from 'unsigned char' to regular 'char'  */
  const char *result = (const char *)sqlite3_column_text(stmt->s3stmt, column);

  if (result_pool && result != NULL)
    result = apr_pstrdup(result_pool, result);

  return result;
}

svn_revnum_t
svn_sqlite__column_revnum(svn_sqlite__stmt_t *stmt, int column)
{
  if (svn_sqlite__column_is_null(stmt, column))
    return SVN_INVALID_REVNUM;
  return (svn_revnum_t) sqlite3_column_int64(stmt->s3stmt, column);
}

svn_boolean_t
svn_sqlite__column_boolean(svn_sqlite__stmt_t *stmt, int column)
{
  return sqlite3_column_int64(stmt->s3stmt, column) != 0;
}

int
svn_sqlite__column_int(svn_sqlite__stmt_t *stmt, int column)
{
  return sqlite3_column_int(stmt->s3stmt, column);
}

apr_int64_t
svn_sqlite__column_int64(svn_sqlite__stmt_t *stmt, int column)
{
  return sqlite3_column_int64(stmt->s3stmt, column);
}

int
svn_sqlite__column_token(svn_sqlite__stmt_t *stmt,
                         int column,
                         const svn_token_map_t *map)
{
  /* cast from 'unsigned char' to regular 'char'  */
  const char *word = (const char *)sqlite3_column_text(stmt->s3stmt, column);

  return svn_token__from_word_strict(map, word);
}

int
svn_sqlite__column_token_null(svn_sqlite__stmt_t *stmt,
                              int column,
                              const svn_token_map_t *map,
                              int null_val)
{
  /* cast from 'unsigned char' to regular 'char'  */
  const char *word = (const char *)sqlite3_column_text(stmt->s3stmt, column);

  if (!word)
    return null_val;

  return svn_token__from_word_strict(map, word);
}

svn_error_t *
svn_sqlite__column_properties(apr_hash_t **props,
                              svn_sqlite__stmt_t *stmt,
                              int column,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  apr_size_t len;
  const void *val;

  /* svn_skel__parse_proplist copies everything needed to result_pool */
  val = svn_sqlite__column_blob(stmt, column, &len, NULL);
  if (val == NULL)
    {
      *props = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_skel__parse_proplist(props,
                                   svn_skel__parse(val, len, scratch_pool),
                                   result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__column_iprops(apr_array_header_t **iprops,
                          svn_sqlite__stmt_t *stmt,
                          int column,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_size_t len;
  const void *val;

  /* svn_skel__parse_iprops copies everything needed to result_pool */
  val = svn_sqlite__column_blob(stmt, column, &len, NULL);
  if (val == NULL)
    {
      *iprops = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_skel__parse_iprops(iprops,
                                 svn_skel__parse(val, len, scratch_pool),
                                 result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__column_checksum(const svn_checksum_t **checksum,
                            svn_sqlite__stmt_t *stmt, int column,
                            apr_pool_t *result_pool)
{
  const char *digest = svn_sqlite__column_text(stmt, column, NULL);

  if (digest == NULL)
    *checksum = NULL;
  else
    SVN_ERR(svn_checksum_deserialize(checksum, digest,
                                     result_pool, result_pool));

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_sqlite__column_is_null(svn_sqlite__stmt_t *stmt, int column)
{
  return sqlite3_column_type(stmt->s3stmt, column) == SQLITE_NULL;
}

int
svn_sqlite__column_bytes(svn_sqlite__stmt_t *stmt, int column)
{
  return sqlite3_column_bytes(stmt->s3stmt, column);
}

svn_error_t *
svn_sqlite__finalize(svn_sqlite__stmt_t *stmt)
{
  SQLITE_ERR(sqlite3_finalize(stmt->s3stmt), stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__reset(svn_sqlite__stmt_t *stmt)
{
  /* No need to reset again after a first attempt */
  stmt->needs_reset = FALSE;

  /* Clear bindings first, as there are no documented reasons
     why this would ever fail, but keeping variable bindings
     when reset is not what we expect. */
  SQLITE_ERR(sqlite3_clear_bindings(stmt->s3stmt), stmt->db);

  /* Reset last, as this *will* fail if the statement failed since
     the last time it was reset, while reporting just the same failure.
     (In this case the statement is also properly reset).

     See the sqlite3_reset() documentation for more details. */
  SQLITE_ERR(sqlite3_reset(stmt->s3stmt), stmt->db);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_sqlite__read_schema_version(int *version,
                                svn_sqlite__db_t *db,
                                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(prepare_statement(&stmt, db, "PRAGMA user_version;", scratch_pool));
  SVN_ERR(svn_sqlite__step_row(stmt));

  *version = svn_sqlite__column_int(stmt, 0);

  return svn_error_trace(svn_sqlite__finalize(stmt));
}


static volatile svn_atomic_t sqlite_init_state = 0;

/* If possible, verify that SQLite was compiled in a thread-safe
   manner. */
/* Don't call this function directly!  Use svn_atomic__init_once(). */
static svn_error_t *
init_sqlite(void *baton, apr_pool_t *pool)
{
  if (sqlite3_libversion_number() < SVN_SQLITE_MIN_VERSION_NUMBER)
    {
      return svn_error_createf(
                    SVN_ERR_SQLITE_ERROR, NULL,
                    _("SQLite compiled for %s, but running with %s"),
                    SVN_SQLITE_MIN_VERSION, sqlite3_libversion());
    }

#if APR_HAS_THREADS

  /* SQLite 3.5 allows verification of its thread-safety at runtime.
     Older versions are simply expected to have been configured with
     --enable-threadsafe, which compiles with -DSQLITE_THREADSAFE=1
     (or -DTHREADSAFE, for older versions). */
  if (! sqlite3_threadsafe())
    return svn_error_create(SVN_ERR_SQLITE_ERROR, NULL,
                            _("SQLite is required to be compiled and run in "
                              "thread-safe mode"));

  /* If SQLite has been already initialized, sqlite3_config() returns
     SQLITE_MISUSE. */
  {
    int err = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
    if (err != SQLITE_OK && err != SQLITE_MISUSE)
      return svn_error_createf(SQLITE_ERROR_CODE(err), NULL,
                               _("Could not configure SQLite [S%d]"), err);
  }
  SQLITE_ERR_MSG(sqlite3_initialize(), _("Could not initialize SQLite"));

#endif /* APR_HAS_THRADS */

  return SVN_NO_ERROR;
}

static svn_error_t *
internal_open(svn_sqlite__db_t *db, const char *path, svn_sqlite__mode_t mode,
              apr_int32_t timeout, apr_pool_t *scratch_pool)
{
  {
    int flags;

    if (mode == svn_sqlite__mode_readonly)
      flags = SQLITE_OPEN_READONLY;
    else if (mode == svn_sqlite__mode_readwrite)
      flags = SQLITE_OPEN_READWRITE;
    else if (mode == svn_sqlite__mode_rwcreate)
      flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    else
      SVN_ERR_MALFUNCTION();

    /* Turn off SQLite's mutexes. All svn objects are single-threaded,
       so we can already guarantee that our use of the SQLite handle
       will be serialized properly.

       Note: in 3.6.x, we've already config'd SQLite into MULTITHREAD mode,
       so this is probably redundant, but if we are running in a process where
       somebody initialized SQLite before us it is needed anyway.  */
    flags |= SQLITE_OPEN_NOMUTEX;

#if !defined(WIN32) && !defined(SVN_SQLITE_INLINE)
    if (mode == svn_sqlite__mode_rwcreate)
      {
        svn_node_kind_t kind;

        /* Create the file before SQLite to avoid any permissions
           problems with an SQLite build that uses the default
           SQLITE_DEFAULT_FILE_PERMISSIONS of 644 modified by umask.
           We simply want umask permissions. */
        SVN_ERR(svn_io_check_path(path, &kind, scratch_pool));
        if (kind == svn_node_none)
          {
            /* Another thread may have created the file, that's OK. */
            svn_error_t *err = svn_io_file_create_empty(path, scratch_pool);
            if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
              return svn_error_trace(err);
            svn_error_clear(err);
          }
      }
#endif

    /* Open the database. Note that a handle is returned, even when an error
       occurs (except for out-of-memory); thus, we can safely use it to
       extract an error message and construct an svn_error_t.  SQLite always
       requires sqlite3_close() after sqlite3_open_v2() while Subversion
       typically does not require close() after an open() that returns an
       error.  So we must ensure we close the handle if this function, or
       the caller svn_sqlite__open, returns an error to the application. */
    {
      const char *vFs = NULL;

#if defined(WIN32) && SQLITE_VERSION_AT_LEAST(3, 8, 1)
      if (strlen(path) > 248)
        {
          WCHAR *win_path;
          vFs = "win32-longpath"; /* Enable long paths in sqlite */

          /* Long paths must be absolute */
          if (!svn_dirent_is_absolute(path))
            SVN_ERR(svn_dirent_get_absolute(&path, path, scratch_pool));

          /* Convert the path to a properly canonicalized \\?\C:\long\path */
          SVN_ERR(svn_io__utf8_to_unicode_longpath(&win_path, path,
                                                   scratch_pool));

          /* And convert it back to UTF-8 because there is no
              sqlite3_open16_v2() yet */
          SVN_ERR(svn_utf__win32_utf16_to_utf8(&path, win_path, NULL,
                                               scratch_pool));
        }
#endif

      /* ### SQLITE_CANTOPEN */
      SQLITE_ERR_CLOSE(sqlite3_open_v2(path, &db->db3, flags, vFs),
                       db, scratch_pool);
    }
  }

  if (timeout <= 0)
    timeout = BUSY_TIMEOUT;

  /* Retry until timeout when database is busy. */
  SQLITE_ERR_CLOSE(sqlite3_busy_timeout(db->db3, timeout),
                   db, scratch_pool);

  return SVN_NO_ERROR;
}


/* APR cleanup function used to close the database when its pool is destroyed.
   DATA should be the svn_sqlite__db_t handle for the database. */
static apr_status_t
close_apr(void *data)
{
  svn_sqlite__db_t *db = data;
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t result;
  int i;

  /* Check to see if we've already closed this database. */
  if (db->db3 == NULL)
    return APR_SUCCESS;

  /* Finalize any prepared statements. */
  if (db->prepared_stmts)
    {
      for (i = 0; i < db->nbr_statements + STMT_INTERNAL_LAST; i++)
        {
          if (db->prepared_stmts[i])
            {
              if (i < db->nbr_statements
                  && db->prepared_stmts[i]->needs_reset)
                {
#ifdef SVN_DEBUG
                  const char *stmt_text = db->statement_strings[i];
                  SVN_UNUSED(stmt_text);

                  SVN_ERR_MALFUNCTION_NO_RETURN();
#else
                  err = svn_error_compose_create(err,
                            svn_sqlite__reset(db->prepared_stmts[i]));
#endif
                }
              err = svn_error_compose_create(
                        svn_sqlite__finalize(db->prepared_stmts[i]), err);
            }
        }
    }

  result = sqlite3_close(db->db3);

  /* If there's a pre-existing error, return it. */
  if (err)
    {
      result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  if (result != SQLITE_OK)
    return SQLITE_ERROR_CODE(result); /* ### lossy */

  db->db3 = NULL;

  return APR_SUCCESS;
}

#ifdef SVN_UNICODE_NORMALIZATION_FIXES
/* Unicode normalizing collation for WC paths */
static int
collate_ucs_nfd(void *baton,
                int len1, const void *key1,
                int len2, const void *key2)
{
  svn_sqlite__db_t *db = baton;
  int result;

  if (svn_utf__normcmp(key1, len1, key2, len2,
                       &db->sqlext_buf1, &db->sqlext_buf2, &result))
    {
      /* There is really nothing we can do here if an error occurs
         during Unicode normalizetion, and attempting to recover could
         result in the wc.db index being corrupted. Presumably this
         can only happen if the index already contains invalid UTF-8
         strings, which should never happen in any case ... */
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }

  return result;
}

static void
glob_like_ucs_nfd_common(sqlite3_context *context,
                         int argc, sqlite3_value **argv,
                         svn_boolean_t sql_like)
{
  svn_sqlite__db_t *const db = sqlite3_user_data(context);

  const char *const pattern = (void*)sqlite3_value_text(argv[0]);
  const apr_size_t pattern_len = sqlite3_value_bytes(argv[0]);
  const char *const string = (void*)sqlite3_value_text(argv[1]);
  const apr_size_t string_len = sqlite3_value_bytes(argv[1]);

  const char *escape = NULL;
  apr_size_t escape_len = 0;

  svn_boolean_t match;
  svn_error_t *err;

  if (pattern_len > SQLITE_MAX_LIKE_PATTERN_LENGTH)
    {
      sqlite3_result_error(context, "LIKE or GLOB pattern too complex", -1);
      return;
    }

  if (argc == 3 && sql_like)
    {
      escape = (void*)sqlite3_value_text(argv[2]);
      escape_len = sqlite3_value_bytes(argv[2]);
    }

  if (pattern && string)
    {
      err = svn_utf__glob(pattern, pattern_len, string, string_len,
                          escape, escape_len, sql_like,
                          &db->sqlext_buf1, &db->sqlext_buf2, &db->sqlext_buf3,
                          &match);

      if (err)
        {
          const char *errmsg;
          svn_membuf__ensure(&db->sqlext_buf1, 512);
          errmsg = svn_err_best_message(err,
                                        db->sqlext_buf1.data,
                                        db->sqlext_buf1.size - 1);
          svn_error_clear(err);
          sqlite3_result_error(context, errmsg, -1);
          return;
        }

      sqlite3_result_int(context, match);
    }
}

/* Unicode normalizing implementation of GLOB */
static void
glob_ucs_nfd(sqlite3_context *context,
             int argc, sqlite3_value **argv)
{
  glob_like_ucs_nfd_common(context, argc, argv, FALSE);
}

/* Unicode normalizing implementation of LIKE */
static void
like_ucs_nfd(sqlite3_context *context,
             int argc, sqlite3_value **argv)
{
  glob_like_ucs_nfd_common(context, argc, argv, TRUE);
}
#endif /* SVN_UNICODE_NORMALIZATION_FIXES */

svn_error_t *
svn_sqlite__open(svn_sqlite__db_t **db, const char *path,
                 svn_sqlite__mode_t mode, const char * const statements[],
                 int unused1, const char * const *unused2,
                 apr_int32_t timeout,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_atomic__init_once(&sqlite_init_state,
                                init_sqlite, NULL, scratch_pool));

  *db = apr_pcalloc(result_pool, sizeof(**db));

  SVN_ERR(internal_open(*db, path, mode, timeout, scratch_pool));

#if SQLITE_VERSION_NUMBER >= 3008000 && SQLITE_VERSION_NUMBER < 3009000
  /* disable SQLITE_ENABLE_STAT3/4 from 3.8.1 - 3.8.3 (but not 3.8.3.1+)
   * to prevent using it when it's buggy.
   * See: https://www.sqlite.org/src/info/4c86b126f2 */
  if (sqlite3_libversion_number() > 3008000 &&
      sqlite3_libversion_number() < 3008004 &&
      strcmp(sqlite3_sourceid(),"2014-02-11")<0)
    {
      sqlite3_test_control(SQLITE_TESTCTRL_OPTIMIZATIONS, (*db)->db3, 0x800);
    }
#endif

#ifdef SVN_UNICODE_NORMALIZATION_FIXES
  /* Create extension buffers with space for 200 UCS-4 characters. */
  svn_membuf__create(&(*db)->sqlext_buf1, 800, result_pool);
  svn_membuf__create(&(*db)->sqlext_buf2, 800, result_pool);
  svn_membuf__create(&(*db)->sqlext_buf3, 800, result_pool);

  /* Register collation and LIKE and GLOB operator replacements. */
  SQLITE_ERR_CLOSE(sqlite3_create_collation((*db)->db3,
                                            "svn-ucs-nfd", SQLITE_UTF8,
                                            *db, collate_ucs_nfd),
                   db, scratch_pool);
  /* ### Is it really necessary to override these functions?
         I would assume the default implementation to be collation agnostic?
         And otherwise our implementation should be...

         The default implementation is in some cases index backed, while our
         implementation can't be. With an index based on the collation it could
         be. */
  SQLITE_ERR_CLOSE(sqlite3_create_function((*db)->db3, "glob", 2,
                                           SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                           *db, glob_ucs_nfd, NULL, NULL),
                   db, scratch_pool);
  SQLITE_ERR_CLOSE(sqlite3_create_function((*db)->db3, "like", 2,
                                           SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                           *db, like_ucs_nfd, NULL, NULL),
                   db, scratch_pool);
  SQLITE_ERR_CLOSE(sqlite3_create_function((*db)->db3, "like", 3,
                                           SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                           *db, like_ucs_nfd, NULL, NULL),
                   db, scratch_pool);
#endif /* SVN_UNICODE_NORMALIZATION_FIXES */

#ifdef SQLITE3_DEBUG
  sqlite3_trace((*db)->db3, sqlite_tracer, (*db)->db3);
#endif
#ifdef SQLITE3_PROFILE
  sqlite3_profile((*db)->db3, sqlite_profiler, (*db)->db3);
#endif

  SVN_SQLITE__ERR_CLOSE(exec_sql(*db,
              /* The default behavior of the LIKE operator is to ignore case
                 for ASCII characters. Hence, by default 'a' LIKE 'A' is true.
                 The case_sensitive_like pragma installs a new application-
                 defined LIKE function that is either case sensitive or
                 insensitive depending on the value of the case_sensitive_like
                 pragma. */
              "PRAGMA case_sensitive_like=1;"
              /* Disable synchronization to disable the explicit disk flushes
                 that make Sqlite up to 50 times slower; especially on small
                 transactions.

                 This removes some stability guarantees on specific hardware
                 and power failures, but still guarantees atomic commits on
                 application crashes. With our dependency on external data
                 like pristine files (Wc) and revision files (repository),
                 we can't keep up these additional guarantees anyway.

                 ### Maybe switch to NORMAL(1) when we use larger transaction
                     scopes */
              "PRAGMA synchronous=OFF;"
              /* Enable recursive triggers so that a user trigger will fire
                 in the deletion phase of an INSERT OR REPLACE statement.
                 Requires SQLite >= 3.6.18  */
              "PRAGMA recursive_triggers=ON;"
              /* Enforce current Sqlite default behavior. Some distributions
                 might change the Sqlite defaults without realizing how this
                 affects application(read: Subversion) performance/behavior. */
              "PRAGMA foreign_keys=OFF;"      /* SQLITE_DEFAULT_FOREIGN_KEYS*/
              "PRAGMA locking_mode = NORMAL;" /* SQLITE_DEFAULT_LOCKING_MODE */
              /* Testing shows TRUNCATE is faster than DELETE on Windows. */
              "PRAGMA journal_mode = TRUNCATE;"
              ),
                *db);

#if defined(SVN_DEBUG)
  /* When running in debug mode, enable the checking of foreign key
     constraints.  This has possible performance implications, so we don't
     bother to do it for production...for now. */
  SVN_SQLITE__ERR_CLOSE(exec_sql(*db, "PRAGMA foreign_keys=ON;"),
                        *db);
#endif

#ifdef SVN_SQLITE_REVERSE_UNORDERED_SELECTS
  /* When enabled, this PRAGMA causes SELECT statements without an ORDER BY
     clause to emit their results in the reverse order of what they normally
     would.  This can help detecting invalid assumptions about the result
     order.*/
  SVN_SQLITE__ERR_CLOSE(exec_sql(*db, "PRAGMA reverse_unordered_selects=ON;"),
                        *db);
#endif

  /* Store temporary tables in RAM instead of in temporary files, but don't
     fail on this if this option is disabled in the sqlite compilation by
     setting SQLITE_TEMP_STORE to 0 (always to disk) */
  svn_error_clear(exec_sql(*db, "PRAGMA temp_store = MEMORY;"));

  /* Store the provided statements. */
  if (statements)
    {
      (*db)->statement_strings = statements;
      (*db)->nbr_statements = 0;
      while (*statements != NULL)
        {
          statements++;
          (*db)->nbr_statements++;
        }

      (*db)->prepared_stmts = apr_pcalloc(
                                  result_pool,
                                  ((*db)->nbr_statements + STMT_INTERNAL_LAST)
                                                * sizeof(svn_sqlite__stmt_t *));
    }
  else
    {
      (*db)->nbr_statements = 0;
      (*db)->prepared_stmts = apr_pcalloc(result_pool,
                                          (0 + STMT_INTERNAL_LAST)
                                                * sizeof(svn_sqlite__stmt_t *));
    }

  (*db)->state_pool = result_pool;
  apr_pool_cleanup_register(result_pool, *db, close_apr, apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__close(svn_sqlite__db_t *db)
{
  apr_status_t result = apr_pool_cleanup_run(db->state_pool, db, close_apr);

  if (result == APR_SUCCESS)
    return SVN_NO_ERROR;

  return svn_error_wrap_apr(result, NULL);
}

static svn_error_t *
reset_all_statements(svn_sqlite__db_t *db,
                     svn_error_t *error_to_wrap)
{
  int i;
  svn_error_t *err;

  /* ### Should we reorder the errors in this specific case
     ### to avoid returning the normal error as top level error? */

  err = svn_error_compose_create(error_to_wrap,
                   svn_error_create(SVN_ERR_SQLITE_RESETTING_FOR_ROLLBACK,
                                    NULL, NULL));

  for (i = 0; i < db->nbr_statements; i++)
    if (db->prepared_stmts[i] && db->prepared_stmts[i]->needs_reset)
      err = svn_error_compose_create(err,
                                svn_sqlite__reset(db->prepared_stmts[i]));

  return err;
}

static svn_error_t *
rollback_transaction(svn_sqlite__db_t *db,
                     svn_error_t *error_to_wrap)
{
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;

  err = get_internal_statement(&stmt, db, STMT_INTERNAL_ROLLBACK_TRANSACTION);
  if (!err)
    {
      err = svn_error_trace(svn_sqlite__step_done(stmt));

      if (err && err->apr_err == SVN_ERR_SQLITE_BUSY)
        {
          /* ### Houston, we have a problem!

             We are trying to rollback but we can't because some
             statements are still busy. This leaves the database
             unusable for future transactions as the current transaction
             is still open.

             As we are returning the actual error as the most relevant
             error in the chain, our caller might assume that it can
             retry/compensate on this error (e.g. SVN_WC_LOCKED), while
             in fact the SQLite database is unusable until the statements
             started within this transaction are reset and the transaction
             aborted.

             We try to compensate by resetting all prepared but unreset
             statements; but we leave the busy error in the chain anyway to
             help diagnosing the original error and help in finding where
             a reset statement is missing. */
          err = svn_error_trace(reset_all_statements(db, err));
          err = svn_error_compose_create(
                      svn_error_trace(svn_sqlite__step_done(stmt)),
                      err);
        }
    }

  if (err)
    {
      /* Rollback failed, use a specific error code. */
      err = svn_error_create(SVN_ERR_SQLITE_ROLLBACK_FAILED, err, NULL);
    }

  return svn_error_compose_create(error_to_wrap, err);
}

svn_error_t *
svn_sqlite__begin_transaction(svn_sqlite__db_t *db)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(get_internal_statement(&stmt, db,
                                 STMT_INTERNAL_BEGIN_TRANSACTION));
  SVN_ERR(svn_sqlite__step_done(stmt));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__begin_immediate_transaction(svn_sqlite__db_t *db)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(get_internal_statement(&stmt, db,
                                 STMT_INTERNAL_BEGIN_IMMEDIATE_TRANSACTION));
  SVN_ERR(svn_sqlite__step_done(stmt));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__begin_savepoint(svn_sqlite__db_t *db)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(get_internal_statement(&stmt, db,
                                 STMT_INTERNAL_SAVEPOINT_SVN));
  SVN_ERR(svn_sqlite__step_done(stmt));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__finish_transaction(svn_sqlite__db_t *db,
                               svn_error_t *err)
{
  svn_sqlite__stmt_t *stmt;

  /* Commit or rollback the sqlite transaction. */
  if (err)
    {
      return svn_error_trace(rollback_transaction(db, err));
    }
  else
    {
      err = get_internal_statement(&stmt, db,
                                   STMT_INTERNAL_COMMIT_TRANSACTION);
      if (!err)
        err = svn_error_trace(svn_sqlite__step_done(stmt));

      /* Need to rollback if the commit fails as well, because otherwise the
         db connection will be left in an unusable state.

         One important case to keep in mind is trying to COMMIT with concurrent
         readers. In case the commit fails, because someone else is holding a
         shared lock, sqlite keeps the transaction, and *also* keeps the file
         locks on the database. While the first part only prevents from using
         this connection, the second part prevents everyone else from accessing
         the database while the connection is open.

         See https://www.sqlite.org/lang_transaction.html

         COMMIT might also result in an SQLITE_BUSY return code if an another
         thread or process has a shared lock on the database that prevented
         the database from being updated. When COMMIT fails in this way, the
         transaction remains active and the COMMIT can be retried later after
         the reader has had a chance to clear. */
      if (err)
        return svn_error_trace(rollback_transaction(db, err));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__finish_savepoint(svn_sqlite__db_t *db,
                             svn_error_t *err)
{
  svn_sqlite__stmt_t *stmt;

  if (err)
    {
      svn_error_t *err2;

      err2 = get_internal_statement(&stmt, db,
                                    STMT_INTERNAL_ROLLBACK_TO_SAVEPOINT_SVN);

      if (!err2)
        {
          err2 = svn_error_trace(svn_sqlite__step_done(stmt));

          if (err2 && err2->apr_err == SVN_ERR_SQLITE_BUSY)
            {
              /* Ok, we have a major problem. Some statement is still open,
                 which makes it impossible to release this savepoint.

                 ### See huge comment in rollback_transaction() for
                     further details */

              err2 = svn_error_trace(reset_all_statements(db, err2));
              err2 = svn_error_compose_create(
                          svn_error_trace(svn_sqlite__step_done(stmt)),
                          err2);
            }
        }

      err = svn_error_compose_create(err, err2);
      err2 = get_internal_statement(&stmt, db,
                                    STMT_INTERNAL_RELEASE_SAVEPOINT_SVN);

      if (!err2)
        err2 = svn_error_trace(svn_sqlite__step_done(stmt));

      return svn_error_compose_create(err, err2);
    }

  SVN_ERR(get_internal_statement(&stmt, db,
                                 STMT_INTERNAL_RELEASE_SAVEPOINT_SVN));

  /* ### Releasing a savepoint can fail and leave the db connection
         unusable; see svn_sqlite__finish_transaction(). */
  return svn_error_trace(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_sqlite__with_transaction(svn_sqlite__db_t *db,
                             svn_sqlite__transaction_callback_t cb_func,
                             void *cb_baton,
                             apr_pool_t *scratch_pool /* NULL allowed */)
{
  SVN_SQLITE__WITH_TXN(cb_func(cb_baton, db, scratch_pool), db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__with_immediate_transaction(
  svn_sqlite__db_t *db,
  svn_sqlite__transaction_callback_t cb_func,
  void *cb_baton,
  apr_pool_t *scratch_pool /* NULL allowed */)
{
  SVN_SQLITE__WITH_IMMEDIATE_TXN(cb_func(cb_baton, db, scratch_pool), db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__with_lock(svn_sqlite__db_t *db,
                      svn_sqlite__transaction_callback_t cb_func,
                      void *cb_baton,
                      apr_pool_t *scratch_pool /* NULL allowed */)
{
  SVN_SQLITE__WITH_LOCK(cb_func(cb_baton, db, scratch_pool), db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_sqlite__hotcopy(const char *src_path,
                    const char *dst_path,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *src_db;

  SVN_ERR(svn_sqlite__open(&src_db, src_path, svn_sqlite__mode_readonly,
                           NULL, 0, NULL, 0,
                           scratch_pool, scratch_pool));

  {
    svn_sqlite__db_t *dst_db;
    sqlite3_backup *backup;
    int rc1, rc2;

    SVN_ERR(svn_sqlite__open(&dst_db, dst_path, svn_sqlite__mode_rwcreate,
                             NULL, 0, NULL, 0, scratch_pool, scratch_pool));
    backup = sqlite3_backup_init(dst_db->db3, "main", src_db->db3, "main");
    if (!backup)
      return svn_error_createf(SVN_ERR_SQLITE_ERROR, NULL,
                               _("SQLite hotcopy failed for %s"), src_path);
    do
      {
        /* Pages are usually 1024 byte (SQLite docs). On my laptop
           copying gets faster as the number of pages is increased up
           to about 64, beyond that speed levels off.  Lets put the
           number of pages an order of magnitude higher, this is still
           likely to be a fraction of large databases. */
        rc1 = sqlite3_backup_step(backup, 1024);

        /* Should we sleep on SQLITE_OK?  That would make copying a
           large database take much longer.  When we do sleep how,
           long should we sleep?  Should the sleep get longer if we
           keep getting BUSY/LOCKED?  I have no real reason for
           choosing 25. */
        if (rc1 == SQLITE_BUSY || rc1 == SQLITE_LOCKED)
          sqlite3_sleep(25);
      }
    while (rc1 == SQLITE_OK || rc1 == SQLITE_BUSY || rc1 == SQLITE_LOCKED);
    rc2 = sqlite3_backup_finish(backup);
    if (rc1 != SQLITE_DONE)
      SQLITE_ERR(rc1, dst_db);
    SQLITE_ERR(rc2, dst_db);
    SVN_ERR(svn_sqlite__close(dst_db));
  }

  SVN_ERR(svn_sqlite__close(src_db));

  SVN_ERR(svn_io_copy_perms(src_path, dst_path, scratch_pool));

  return SVN_NO_ERROR;
}

struct function_wrapper_baton_t
{
  svn_sqlite__func_t func;
  void *baton;
};

static void
wrapped_func(sqlite3_context *context,
             int argc,
             sqlite3_value *values[])
{
  struct function_wrapper_baton_t *fwb = sqlite3_user_data(context);
  svn_sqlite__context_t sctx;
  svn_error_t *err;
  void *void_values = values;

  sctx.context = context;

  err = fwb->func(&sctx, argc, void_values, fwb->baton);

  if (err)
    {
      char buf[256];
      sqlite3_result_error(context,
                           svn_err_best_message(err, buf, sizeof(buf)),
                           -1);
      svn_error_clear(err);
    }
}


svn_error_t *
svn_sqlite__create_scalar_function(svn_sqlite__db_t *db,
                                   const char *func_name,
                                   int argc,
                                   svn_boolean_t deterministic,
                                   svn_sqlite__func_t func,
                                   void *baton)
{
  int eTextRep;
  struct function_wrapper_baton_t *fwb = apr_pcalloc(db->state_pool,
                                                     sizeof(*fwb));

  fwb->func = func;
  fwb->baton = baton;

  eTextRep = SQLITE_ANY;
  if (deterministic)
    eTextRep |= SQLITE_DETERMINISTIC;

  SQLITE_ERR(sqlite3_create_function(db->db3, func_name, argc, eTextRep,
                                     fwb, wrapped_func, NULL, NULL),
             db);

  return SVN_NO_ERROR;
}

int
svn_sqlite__value_type(svn_sqlite__value_t *val)
{
  void *v = val;
  return sqlite3_value_type(v);
}

const char *
svn_sqlite__value_text(svn_sqlite__value_t *val)
{
  void *v = val;
  return (const char *) sqlite3_value_text(v);
}

void
svn_sqlite__result_null(svn_sqlite__context_t *sctx)
{
  sqlite3_result_null(sctx->context);
}

void
svn_sqlite__result_int64(svn_sqlite__context_t *sctx, apr_int64_t val)
{
  sqlite3_result_int64(sctx->context, val);
}

void
svn_sqlite__result_error(svn_sqlite__context_t *sctx, const char *msg, int num)
{
  sqlite3_result_error(sctx->context, msg, num);
}
