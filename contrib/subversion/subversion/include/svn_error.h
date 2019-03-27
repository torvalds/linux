/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_error.h
 * @brief Common exception handling for Subversion.
 */

#ifndef SVN_ERROR_H
#define SVN_ERROR_H

#include <apr.h>        /* for apr_size_t */
#include <apr_errno.h>  /* APR's error system */
#include <apr_pools.h>  /* for apr_pool_t */

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define APR_WANT_STDIO
#endif
#include <apr_want.h>   /* for FILE* */

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* For the Subversion developers, this #define turns on extended "stack
   traces" of any errors that get thrown. See the SVN_ERR() macro.  */
#ifdef SVN_DEBUG
#define SVN_ERR__TRACING
#endif


/** the best kind of (@c svn_error_t *) ! */
#define SVN_NO_ERROR   0

/* The actual error codes are kept in a separate file; see comments
   there for the reasons why. */
#include "svn_error_codes.h"

/** Put an English description of @a statcode into @a buf and return @a buf,
 * NULL-terminated. @a statcode is either an svn error or apr error.
 */
char *
svn_strerror(apr_status_t statcode,
             char *buf,
             apr_size_t bufsize);


/**
 * Return the symbolic name of an error code.  If the error code
 * is in svn_error_codes.h, return the name of the macro as a string.
 * If the error number is not recognised, return @c NULL.
 *
 * An error number may not be recognised because it was defined in a future
 * version of Subversion (e.g., a 1.9.x server may transmit a defined-in-1.9.0
 * error number to a 1.8.x client).
 *
 * An error number may be recognised @em incorrectly if the @c apr_status_t
 * value originates in another library (such as libserf) which also uses APR.
 * (This is a theoretical concern only: the @c apr_err member of #svn_error_t
 * should never contain a "foreign" @c apr_status_t value, and
 * in any case Subversion and Serf use non-overlapping subsets of the
 * @c APR_OS_START_USERERR range.)
 *
 * Support for error codes returned by APR itself (i.e., not in the
 * @c APR_OS_START_USERERR range, as defined in apr_errno.h) may be implemented
 * in the future.
 *
 * @note In rare cases, a single numeric code has more than one symbolic name.
 * (For example, #SVN_ERR_WC_NOT_DIRECTORY and #SVN_ERR_WC_NOT_WORKING_COPY).
 * In those cases, it is not guaranteed which symbolic name is returned.
 *
 * @since New in 1.8.
 */
const char *
svn_error_symbolic_name(apr_status_t statcode);


/** If @a err has a custom error message, return that, otherwise
 * store the generic error string associated with @a err->apr_err into
 * @a buf (terminating with NULL) and return @a buf.
 *
 * @since New in 1.4.
 *
 * @note @a buf and @a bufsize are provided in the interface so that
 * this function is thread-safe and yet does no allocation.
 */
const char *svn_err_best_message(const svn_error_t *err,
                                 char *buf,
                                 apr_size_t bufsize);



/** SVN error creation and destruction.
 *
 * @defgroup svn_error_error_creation_destroy Error creation and destruction
 * @{
 */

/** Create a nested exception structure.
 *
 * Input:  an APR or SVN custom error code,
 *         a "child" error to wrap,
 *         a specific message
 *
 * Returns:  a new error structure (containing the old one).
 *
 * @note Errors are always allocated in a subpool of the global pool,
 *        since an error's lifetime is generally not related to the
 *        lifetime of any convenient pool.  Errors must be freed
 *        with svn_error_clear().  The specific message should be @c NULL
 *        if there is nothing to add to the general message associated
 *        with the error code.
 *
 *        If creating the "bottommost" error in a chain, pass @c NULL for
 *        the child argument.
 */
svn_error_t *
svn_error_create(apr_status_t apr_err,
                 svn_error_t *child,
                 const char *message);

/** Create an error structure with the given @a apr_err and @a child,
 * with a printf-style error message produced by passing @a fmt, using
 * apr_psprintf().
 */
svn_error_t *
svn_error_createf(apr_status_t apr_err,
                  svn_error_t *child,
                  const char *fmt,
                  ...)
  __attribute__ ((format(printf, 3, 4)));

/** Wrap a @a status from an APR function.  If @a fmt is NULL, this is
 * equivalent to svn_error_create(status,NULL,NULL).  Otherwise,
 * the error message is constructed by formatting @a fmt and the
 * following arguments according to apr_psprintf(), and then
 * appending ": " and the error message corresponding to @a status.
 * (If UTF-8 translation of the APR error message fails, the ": " and
 * APR error are not appended to the error message.)
 */
svn_error_t *
svn_error_wrap_apr(apr_status_t status,
                   const char *fmt,
                   ...)
       __attribute__((format(printf, 2, 3)));

/** If @a child is SVN_NO_ERROR, return SVN_NO_ERROR.
 * Else, prepend a new error to the error chain of @a child. The new error
 * uses @a new_msg as error message but all other error attributes (such
 * as the error code) are copied from @a child.
 */
svn_error_t *
svn_error_quick_wrap(svn_error_t *child,
                     const char *new_msg);

/** Like svn_error_quick_wrap(), but with format string support.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_error_quick_wrapf(svn_error_t *child,
                      const char *fmt,
                      ...)
       __attribute__((format(printf, 2, 3)));

/** Compose two errors, returning the composition as a brand new error
 * and consuming the original errors.  Either or both of @a err1 and
 * @a err2 may be @c SVN_NO_ERROR.  If both are not @c SVN_NO_ERROR,
 * @a err2 will follow @a err1 in the chain of the returned error.
 *
 * Either @a err1 or @a err2 can be functions that return svn_error_t*
 * but if both are functions they can be evaluated in either order as
 * per the C language rules.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_error_compose_create(svn_error_t *err1,
                         svn_error_t *err2);

/** Add @a new_err to the end of @a chain's chain of errors.  The @a new_err
 * chain will be copied into @a chain's pool and destroyed, so @a new_err
 * itself becomes invalid after this function.
 *
 * Either @a chain or @a new_err can be functions that return svn_error_t*
 * but if both are functions they can be evaluated in either order as
 * per the C language rules.
 */
void
svn_error_compose(svn_error_t *chain,
                  svn_error_t *new_err);

/** Return the root cause of @a err by finding the last error in its
 * chain (e.g. it or its children).  @a err may be @c SVN_NO_ERROR, in
 * which case @c SVN_NO_ERROR is returned.  The returned error should
 * @em not be cleared as it shares memory with @a err.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_error_root_cause(svn_error_t *err);

/** Return the first error in @a err's chain that has an error code @a
 * apr_err or #SVN_NO_ERROR if there is no error with that code.  The
 * returned error should @em not be cleared as it shares memory with @a err.
 *
 * If @a err is #SVN_NO_ERROR, return #SVN_NO_ERROR.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_error_find_cause(svn_error_t *err, apr_status_t apr_err);

/** Create a new error that is a deep copy of @a err and return it.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_error_dup(const svn_error_t *err);

/** Free the memory used by @a error, as well as all ancestors and
 * descendants of @a error.
 *
 * Unlike other Subversion objects, errors are managed explicitly; you
 * MUST clear an error if you are ignoring it, or you are leaking memory.
 * For convenience, @a error may be @c NULL, in which case this function does
 * nothing; thus, svn_error_clear(svn_foo(...)) works as an idiom to
 * ignore errors.
 */
void
svn_error_clear(svn_error_t *error);


#if defined(SVN_ERR__TRACING)
/** Set the error location for debug mode. */
void
svn_error__locate(const char *file,
                  long line);

/* Wrapper macros to collect file and line information */
#define svn_error_create \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_create))
#define svn_error_createf \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_createf))
#define svn_error_wrap_apr \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_wrap_apr))
#define svn_error_quick_wrap \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_quick_wrap))
#define svn_error_quick_wrapf \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_quick_wrapf))
#endif


/**
 * Very basic default error handler: print out error stack @a error to the
 * stdio stream @a stream, with each error prefixed by @a prefix; quit and
 * clear @a error iff the @a fatal flag is set.  Allocations are performed
 * in the @a error's pool.
 *
 * If you're not sure what prefix to pass, just pass "svn: ".  That's
 * what code that used to call svn_handle_error() and now calls
 * svn_handle_error2() does.
 *
 * Note that this should only be used from commandline specific code, or
 * code that knows that @a stream is really where the application wants
 * to receive its errors on.
 *
 * @since New in 1.2.
 */
void
svn_handle_error2(svn_error_t *error,
                  FILE *stream,
                  svn_boolean_t fatal,
                  const char *prefix);

/** Like svn_handle_error2() but with @c prefix set to "svn: "
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
void
svn_handle_error(svn_error_t *error,
                 FILE *stream,
                 svn_boolean_t fatal);

/**
 * Very basic default warning handler: print out the error @a error to the
 * stdio stream @a stream, prefixed by @a prefix.  Allocations are
 * performed in the error's pool.
 *
 * @a error may not be @c NULL.
 *
 * @note This does not clear @a error.
 *
 * @since New in 1.2.
 */
void
svn_handle_warning2(FILE *stream,
                    const svn_error_t *error,
                    const char *prefix);

/** Like svn_handle_warning2() but with @c prefix set to "svn: "
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
void
svn_handle_warning(FILE *stream,
                   svn_error_t *error);


/** A statement macro for checking error values.
 *
 * Evaluate @a expr.  If it yields an error, return that error from the
 * current function.  Otherwise, continue.
 *
 * The <tt>do { ... } while (0)</tt> wrapper has no semantic effect,
 * but it makes this macro syntactically equivalent to the expression
 * statement it resembles.  Without it, statements like
 *
 * @code
 *   if (a)
 *     SVN_ERR(some operation);
 *   else
 *     foo;
 * @endcode
 *
 * would not mean what they appear to.
 */
#define SVN_ERR(expr)                           \
  do {                                          \
    svn_error_t *svn_err__temp = (expr);        \
    if (svn_err__temp)                          \
      return svn_error_trace(svn_err__temp);    \
  } while (0)

/**
 * A macro for wrapping an error in a source-location trace message.
 *
 * This macro can be used when directly returning an already created
 * error (when not using SVN_ERR, svn_error_create(), etc.) to ensure
 * that the call stack is recorded correctly.
 *
 * @since New in 1.7.
 */
#ifdef SVN_ERR__TRACING
svn_error_t *
svn_error__trace(const char *file, long line, svn_error_t *err);

#define svn_error_trace(expr)  svn_error__trace(__FILE__, __LINE__, (expr))
#else
#define svn_error_trace(expr)  (expr)
#endif

/**
 * Returns an error chain that is based on @a err's error chain but
 * does not include any error tracing placeholders.  @a err is not
 * modified, except for any allocations using its pool.
 *
 * The returned error chain is allocated from @a err's pool and shares
 * its message and source filename character arrays.  The returned
 * error chain should *not* be cleared because it is not a fully
 * fledged error chain, only clearing @a err should be done to clear
 * the returned error chain.  If @a err is cleared, then the returned
 * error chain is unusable.
 *
 * @a err can be #SVN_NO_ERROR.  If @a err is not #SVN_NO_ERROR, then
 * the last link in the error chain must be a non-tracing error, i.e,
 * a real error.
 *
 * @since New in 1.7.
 */
svn_error_t *svn_error_purge_tracing(svn_error_t *err);


/** A statement macro, very similar to @c SVN_ERR.
 *
 * This macro will wrap the error with the specified text before
 * returning the error.
 */
#define SVN_ERR_W(expr, wrap_msg)                           \
  do {                                                      \
    svn_error_t *svn_err__temp = (expr);                    \
    if (svn_err__temp)                                      \
      return svn_error_quick_wrap(svn_err__temp, wrap_msg); \
  } while (0)


/** A statement macro intended for the main() function of the 'svn' program.
 *
 * Evaluate @a expr. If it yields an error, display the error on stdout
 * and return @c EXIT_FAILURE.
 *
 * @note Not for use in the library, as it prints to stderr. This macro
 * no longer suits the needs of the 'svn' program, and is not generally
 * suitable for third-party use as it assumes the program name is 'svn'.
 *
 * @deprecated Provided for backward compatibility with the 1.8 API. Consider
 * using svn_handle_error2() or svn_cmdline_handle_exit_error() instead.
 */
#define SVN_INT_ERR(expr)                                        \
  do {                                                           \
    svn_error_t *svn_err__temp = (expr);                         \
    if (svn_err__temp) {                                         \
      svn_handle_error2(svn_err__temp, stderr, FALSE, "svn: ");  \
      svn_error_clear(svn_err__temp);                            \
      return EXIT_FAILURE; }                                     \
  } while (0)

/** @} */


/** Error groups
 *
 * @defgroup svn_error_error_groups Error groups
 * @{
 */

/**
 * Return TRUE if @a err is an error specifically related to locking a
 * path in the repository, FALSE otherwise.
 *
 * SVN_ERR_FS_OUT_OF_DATE and SVN_ERR_FS_NOT_FOUND are in here because it's a
 * non-fatal error that can be thrown when attempting to lock an item.
 *
 * SVN_ERR_REPOS_HOOK_FAILURE refers to the pre-lock hook.
 *
 * @since New in 1.2.
 */
#define SVN_ERR_IS_LOCK_ERROR(err)                          \
  (err->apr_err == SVN_ERR_FS_PATH_ALREADY_LOCKED ||        \
   err->apr_err == SVN_ERR_FS_NOT_FOUND           ||        \
   err->apr_err == SVN_ERR_FS_OUT_OF_DATE         ||        \
   err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN      ||        \
   err->apr_err == SVN_ERR_REPOS_HOOK_FAILURE     ||        \
   err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION    ||        \
   err->apr_err == SVN_ERR_FS_OUT_OF_DATE         ||        \
   err->apr_err == SVN_ERR_FS_NOT_FILE)

/**
 * Return TRUE if @a err is an error specifically related to unlocking
 * a path in the repository, FALSE otherwise.
 *
 * SVN_ERR_REPOS_HOOK_FAILURE refers to the pre-unlock hook.
 *
 * @since New in 1.2.
 */
#define SVN_ERR_IS_UNLOCK_ERROR(err)                        \
  (err->apr_err == SVN_ERR_FS_PATH_NOT_LOCKED ||            \
   err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN ||             \
   err->apr_err == SVN_ERR_FS_LOCK_OWNER_MISMATCH ||        \
   err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK ||               \
   err->apr_err == SVN_ERR_RA_NOT_LOCKED ||                 \
   err->apr_err == SVN_ERR_FS_LOCK_EXPIRED ||               \
   err->apr_err == SVN_ERR_REPOS_HOOK_FAILURE)

/** Evaluates to @c TRUE iff @a apr_err (of type apr_status_t) is in the given
 * @a category, which should be one of the @c SVN_ERR_*_CATEGORY_START
 * constants.
 *
 * @since New in 1.7.
 */
#define SVN_ERROR_IN_CATEGORY(apr_err, category)            \
    ((category) == ((apr_err) / SVN_ERR_CATEGORY_SIZE) * SVN_ERR_CATEGORY_SIZE)


/** @} */


/** Internal malfunctions and assertions
 *
 * @defgroup svn_error_malfunction_assertion Malfunctions and assertions
 * @{
 */

/** Report that an internal malfunction has occurred, and possibly terminate
 * the program.
 *
 * Act as determined by the current "malfunction handler" which may have
 * been specified by a call to svn_error_set_malfunction_handler() or else
 * is the default handler as specified in that function's documentation. If
 * the malfunction handler returns, then cause the function using this macro
 * to return the error object that it generated.
 *
 * @note The intended use of this macro is where execution reaches a point
 * that cannot possibly be reached unless there is a bug in the program.
 *
 * @since New in 1.6.
 */
#define SVN_ERR_MALFUNCTION()                                      \
  do {                                                             \
    return svn_error_trace(svn_error__malfunction(                 \
                                 TRUE, __FILE__, __LINE__, NULL)); \
  } while (0)

/** Similar to SVN_ERR_MALFUNCTION(), but without the option of returning
 * an error to the calling function.
 *
 * If possible you should use SVN_ERR_MALFUNCTION() instead.
 *
 * @since New in 1.6.
 */
#define SVN_ERR_MALFUNCTION_NO_RETURN()                      \
  do {                                                       \
    svn_error__malfunction(FALSE, __FILE__, __LINE__, NULL); \
    abort();                                                 \
  } while (1)

/** Like SVN_ERR_ASSERT(), but append ERR to the returned error chain.
 *
 * If EXPR is false, return a malfunction error whose chain includes ERR.
 * If EXPR is true, do nothing.  (In particular, this does not clear ERR.)
 *
 * Types: (svn_boolean_t expr, svn_error_t *err)
 *
 * @since New in 1.8.
 */
#ifdef __clang_analyzer__
#include <assert.h>
/* Just ignore ERR.  If the assert triggers, it'll be our least concern. */
#define SVN_ERR_ASSERT_E(expr, err)       assert((expr))
#else
#define SVN_ERR_ASSERT_E(expr, err)                                      \
  do {                                                                  \
    if (!(expr)) {                                                      \
      return svn_error_compose_create(                                  \
               svn_error__malfunction(TRUE, __FILE__, __LINE__, #expr), \
               (err));                                                  \
    }                                                                   \
  } while (0)
#endif


/** Check that a condition is true: if not, report an error and possibly
 * terminate the program.
 *
 * If the Boolean expression @a expr is true, do nothing. Otherwise,
 * act as determined by the current "malfunction handler" which may have
 * been specified by a call to svn_error_set_malfunction_handler() or else
 * is the default handler as specified in that function's documentation. If
 * the malfunction handler returns, then cause the function using this macro
 * to return the error object that it generated.
 *
 * @note The intended use of this macro is to check a condition that cannot
 * possibly be false unless there is a bug in the program.
 *
 * @note The condition to be checked should not be computationally expensive
 * if it is reached often, as, unlike traditional "assert" statements, the
 * evaluation of this expression is not compiled out in release-mode builds.
 *
 * @since New in 1.6.
 *
 * @see SVN_ERR_ASSERT_E()
 */
#ifdef __clang_analyzer__
#include <assert.h>
#define SVN_ERR_ASSERT(expr)       assert((expr))
#else
#define SVN_ERR_ASSERT(expr)                                            \
  do {                                                                  \
    if (!(expr))                                                        \
      SVN_ERR(svn_error__malfunction(TRUE, __FILE__, __LINE__, #expr)); \
  } while (0)
#endif

/** Similar to SVN_ERR_ASSERT(), but without the option of returning
 * an error to the calling function.
 *
 * If possible you should use SVN_ERR_ASSERT() instead.
 *
 * @since New in 1.6.
 */
#define SVN_ERR_ASSERT_NO_RETURN(expr)                          \
  do {                                                          \
    if (!(expr)) {                                              \
      svn_error__malfunction(FALSE, __FILE__, __LINE__, #expr); \
      abort();                                                  \
    }                                                           \
  } while (0)

/** Report a "Not implemented" malfunction.  Internal use only. */
#define SVN__NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")

/** A helper function for the macros that report malfunctions. Handle a
 * malfunction by calling the current "malfunction handler" which may have
 * been specified by a call to svn_error_set_malfunction_handler() or else
 * is the default handler as specified in that function's documentation.
 *
 * Pass all of the parameters to the handler. The error occurred in the
 * source file @a file at line @a line, and was an assertion failure of the
 * expression @a expr, or, if @a expr is null, an unconditional error.
 *
 * If @a can_return is true, the handler can return an error object
 * that is returned by the caller. If @a can_return is false the
 * method should never return. (The caller will call abort())
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_error__malfunction(svn_boolean_t can_return,
                       const char *file,
                       int line,
                       const char *expr);

/** A type of function that handles an assertion failure or other internal
 * malfunction detected within the Subversion libraries.
 *
 * The error occurred in the source file @a file at line @a line, and was an
 * assertion failure of the expression @a expr, or, if @a expr is null, an
 * unconditional error.
 *
 * If @a can_return is false a function of this type must never return.
 *
 * If @a can_return is true a function of this type must do one of:
 *   - Return an error object describing the error, using an error code in
 *     the category SVN_ERR_MALFUNC_CATEGORY_START.
 *   - Never return.
 *
 * The function may alter its behaviour according to compile-time
 * and run-time and even interactive conditions.
 *
 * @see SVN_ERROR_IN_CATEGORY()
 *
 * @since New in 1.6.
 */
typedef svn_error_t *(*svn_error_malfunction_handler_t)
  (svn_boolean_t can_return, const char *file, int line, const char *expr);

/** Cause subsequent malfunctions to be handled by @a func.
 * Return the handler that was previously in effect.
 *
 * @a func may not be null.
 *
 * @note The default handler is svn_error_abort_on_malfunction().
 *
 * @note This function must be called in a single-threaded context.
 *
 * @since New in 1.6.
 */
svn_error_malfunction_handler_t
svn_error_set_malfunction_handler(svn_error_malfunction_handler_t func);

/** Return the malfunction handler that is currently in effect.
 * @since New in 1.9. */
svn_error_malfunction_handler_t
svn_error_get_malfunction_handler(void);

/** Handle a malfunction by returning an error object that describes it.
 *
 * When @a can_return is false, abort()
 *
 * This function implements @c svn_error_malfunction_handler_t.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_error_raise_on_malfunction(svn_boolean_t can_return,
                               const char *file,
                               int line,
                               const char *expr);

/** Handle a malfunction by printing a message to stderr and aborting.
 *
 * This function implements @c svn_error_malfunction_handler_t.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_error_abort_on_malfunction(svn_boolean_t can_return,
                               const char *file,
                               int line,
                               const char *expr);

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ERROR_H */
