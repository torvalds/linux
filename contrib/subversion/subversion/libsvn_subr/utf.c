/*
 * utf.c:  UTF-8 conversion routines
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



#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <apr_strings.h>
#include <apr_lib.h>
#include <apr_xlate.h>
#include <apr_atomic.h>

#include "svn_hash.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ctype.h"
#include "svn_utf.h"
#include "svn_private_config.h"
#include "win32_xlate.h"

#include "private/svn_utf_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_string_private.h"
#include "private/svn_mutex.h"



/* Use these static strings to maximize performance on standard conversions.
 * Any strings on other locations are still valid, however.
 */
static const char *SVN_UTF_NTOU_XLATE_HANDLE = "svn-utf-ntou-xlate-handle";
static const char *SVN_UTF_UTON_XLATE_HANDLE = "svn-utf-uton-xlate-handle";

static const char *SVN_APR_UTF8_CHARSET = "UTF-8";

static svn_mutex__t *xlate_handle_mutex = NULL;
static svn_boolean_t assume_native_charset_is_utf8 = FALSE;

#if defined(WIN32)
typedef svn_subr__win32_xlate_t xlate_handle_t;
#else
typedef apr_xlate_t xlate_handle_t;
#endif

/* The xlate handle cache is a global hash table with linked lists of xlate
 * handles.  In multi-threaded environments, a thread "borrows" an xlate
 * handle from the cache during a translation and puts it back afterwards.
 * This avoids holding a global lock for all translations.
 * If there is no handle for a particular key when needed, a new is
 * handle is created and put in the cache after use.
 * This means that there will be at most N handles open for a key, where N
 * is the number of simultanous handles in use for that key. */

typedef struct xlate_handle_node_t {
  xlate_handle_t *handle;
  /* FALSE if the handle is not valid, since its pool is being
     destroyed. */
  svn_boolean_t valid;
  /* The name of a char encoding or APR_LOCALE_CHARSET. */
  const char *frompage, *topage;
  struct xlate_handle_node_t *next;
} xlate_handle_node_t;

/* This maps const char * userdata_key strings to xlate_handle_node_t **
   handles to the first entry in the linked list of xlate handles.  We don't
   store the pointer to the list head directly in the hash table, since we
   remove/insert entries at the head in the list in the code below, and
   we can't use apr_hash_set() in each character translation because that
   function allocates memory in each call where the value is non-NULL.
   Since these allocations take place in a global pool, this would be a
   memory leak. */
static apr_hash_t *xlate_handle_hash = NULL;

/* "1st level cache" to standard conversion maps. We may access these
 * using atomic xchange ops, i.e. without further thread synchronization.
 * If the respective item is NULL, fallback to hash lookup.
 */
static void * volatile xlat_ntou_static_handle = NULL;
static void * volatile xlat_uton_static_handle = NULL;

/* Clean up the xlate handle cache. */
static apr_status_t
xlate_cleanup(void *arg)
{
  /* We set the cache variables to NULL so that translation works in other
     cleanup functions, even if it isn't cached then. */
  xlate_handle_hash = NULL;

  /* ensure no stale objects get accessed */
  xlat_ntou_static_handle = NULL;
  xlat_uton_static_handle = NULL;

  return APR_SUCCESS;
}

/* Set the handle of ARG to NULL. */
static apr_status_t
xlate_handle_node_cleanup(void *arg)
{
  xlate_handle_node_t *node = arg;

  node->valid = FALSE;
  return APR_SUCCESS;
}

void
svn_utf_initialize2(svn_boolean_t assume_native_utf8,
                    apr_pool_t *pool)
{
  if (!xlate_handle_hash)
    {
      /* We create our own subpool, which we protect with the mutex.
         We can't use the pool passed to us by the caller, since we will
         use it for xlate handle allocations, possibly in multiple threads,
         and pool allocation is not thread-safe. */
      apr_pool_t *subpool = svn_pool_create(pool);
      svn_mutex__t *mutex;
      svn_error_t *err = svn_mutex__init(&mutex, TRUE, subpool);
      if (err)
        {
          svn_error_clear(err);
          return;
        }

      xlate_handle_mutex = mutex;
      xlate_handle_hash = apr_hash_make(subpool);

      apr_pool_cleanup_register(subpool, NULL, xlate_cleanup,
                                apr_pool_cleanup_null);
    }

    if (!assume_native_charset_is_utf8)
      assume_native_charset_is_utf8 = assume_native_utf8;
}

/* Return a unique string key based on TOPAGE and FROMPAGE.  TOPAGE and
 * FROMPAGE can be any valid arguments of the same name to
 * apr_xlate_open().  Allocate the returned string in POOL. */
static const char*
get_xlate_key(const char *topage,
              const char *frompage,
              apr_pool_t *pool)
{
  /* In the cases of SVN_APR_LOCALE_CHARSET and SVN_APR_DEFAULT_CHARSET
   * topage/frompage is really an int, not a valid string.  So generate a
   * unique key accordingly. */
  if (frompage == SVN_APR_LOCALE_CHARSET)
    frompage = "APR_LOCALE_CHARSET";
  else if (frompage == SVN_APR_DEFAULT_CHARSET)
    frompage = "APR_DEFAULT_CHARSET";

  if (topage == SVN_APR_LOCALE_CHARSET)
    topage = "APR_LOCALE_CHARSET";
  else if (topage == SVN_APR_DEFAULT_CHARSET)
    topage = "APR_DEFAULT_CHARSET";

  return apr_pstrcat(pool, "svn-utf-", frompage, "to", topage,
                     "-xlate-handle", SVN_VA_NULL);
}

/* Atomically replace the content in *MEM with NEW_VALUE and return
 * the previous content of *MEM. If atomicy cannot be guaranteed,
 * *MEM will not be modified and NEW_VALUE is simply returned to
 * the caller.
 */
static APR_INLINE void*
atomic_swap(void * volatile * mem, void *new_value)
{
#if APR_HAS_THREADS
   return svn_atomic_xchgptr(mem, new_value);
#else
   /* no threads - no sync. necessary */
   void *old_value = (void*)*mem;
   *mem = new_value;
   return old_value;
#endif
}

/* Set *RET to a newly created handle node for converting from FROMPAGE
   to TOPAGE, If apr_xlate_open() returns APR_EINVAL or APR_ENOTIMPL, set
   (*RET)->handle to NULL.  If fail for any other reason, return the error.
   Allocate *RET and its xlate handle in POOL. */
static svn_error_t *
xlate_alloc_handle(xlate_handle_node_t **ret,
                   const char *topage, const char *frompage,
                   apr_pool_t *pool)
{
  apr_status_t apr_err;
  xlate_handle_t *handle;
  const char *name;

  /* The error handling doesn't support the following cases, since we don't
     use them currently.  Catch this here. */
  SVN_ERR_ASSERT(frompage != SVN_APR_DEFAULT_CHARSET
                 && topage != SVN_APR_DEFAULT_CHARSET
                 && (frompage != SVN_APR_LOCALE_CHARSET
                     || topage != SVN_APR_LOCALE_CHARSET));

  /* Try to create a handle. */
#if defined(WIN32)
  apr_err = svn_subr__win32_xlate_open(&handle, topage,
                                       frompage, pool);
  name = "win32-xlate: ";
#else
  apr_err = apr_xlate_open(&handle, topage, frompage, pool);
  name = "APR: ";
#endif

  if (APR_STATUS_IS_EINVAL(apr_err) || APR_STATUS_IS_ENOTIMPL(apr_err))
    handle = NULL;
  else if (apr_err != APR_SUCCESS)
    {
      const char *errstr;
      char apr_strerr[512];

      /* Can't use svn_error_wrap_apr here because it calls functions in
         this file, leading to infinite recursion. */
      if (frompage == SVN_APR_LOCALE_CHARSET)
        errstr = apr_psprintf(pool,
                              _("Can't create a character converter from "
                                "native encoding to '%s'"), topage);
      else if (topage == SVN_APR_LOCALE_CHARSET)
        errstr = apr_psprintf(pool,
                              _("Can't create a character converter from "
                                "'%s' to native encoding"), frompage);
      else
        errstr = apr_psprintf(pool,
                              _("Can't create a character converter from "
                                "'%s' to '%s'"), frompage, topage);

      /* Just put the error on the stack, since svn_error_create duplicates it
         later.  APR_STRERR will be in the local encoding, not in UTF-8, though.
       */
      svn_strerror(apr_err, apr_strerr, sizeof(apr_strerr));
      return svn_error_createf(SVN_ERR_PLUGIN_LOAD_FAILURE,
                               svn_error_create(apr_err, NULL, apr_strerr),
                               "%s%s", name, errstr);
    }

  /* Allocate and initialize the node. */
  *ret = apr_palloc(pool, sizeof(xlate_handle_node_t));
  (*ret)->handle = handle;
  (*ret)->valid = TRUE;
  (*ret)->frompage = ((frompage != SVN_APR_LOCALE_CHARSET)
                      ? apr_pstrdup(pool, frompage) : frompage);
  (*ret)->topage = ((topage != SVN_APR_LOCALE_CHARSET)
                    ? apr_pstrdup(pool, topage) : topage);
  (*ret)->next = NULL;

  /* If we are called from inside a pool cleanup handler, the just created
     xlate handle will be closed when that handler returns by a newly
     registered cleanup handler, however, the handle is still cached by us.
     To prevent this, we register a cleanup handler that will reset the valid
     flag of our node, so we don't use an invalid handle. */
  if (handle)
    apr_pool_cleanup_register(pool, *ret, xlate_handle_node_cleanup,
                              apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

/* Extend xlate_alloc_handle by using USERDATA_KEY as a key in our
   global hash map, if available.

   Allocate *RET and its xlate handle in POOL if svn_utf_initialize()
   hasn't been called or USERDATA_KEY is NULL.  Else, allocate them
   in the pool of xlate_handle_hash.

   Note: this function is not thread-safe. Call get_xlate_handle_node
   instead. */
static svn_error_t *
get_xlate_handle_node_internal(xlate_handle_node_t **ret,
                               const char *topage, const char *frompage,
                               const char *userdata_key, apr_pool_t *pool)
{
  /* If we already have a handle, just return it. */
  if (userdata_key && xlate_handle_hash)
    {
      xlate_handle_node_t *old_node = NULL;

      /* 2nd level: hash lookup */
      xlate_handle_node_t **old_node_p = svn_hash_gets(xlate_handle_hash,
                                                       userdata_key);
      if (old_node_p)
        old_node = *old_node_p;
      if (old_node)
        {
          /* Ensure that the handle is still valid. */
          if (old_node->valid)
            {
              /* Remove from the list. */
              *old_node_p = old_node->next;
              old_node->next = NULL;
              *ret = old_node;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Note that we still have the mutex locked (if it is initialized), so we
     can use the global pool for creating the new xlate handle. */

  /* Use the correct pool for creating the handle. */
  pool = apr_hash_pool_get(xlate_handle_hash);

  return xlate_alloc_handle(ret, topage, frompage, pool);
}

/* Set *RET to a handle node for converting from FROMPAGE to TOPAGE,
   creating the handle node if it doesn't exist in USERDATA_KEY.
   If a node is not cached and apr_xlate_open() returns APR_EINVAL or
   APR_ENOTIMPL, set (*RET)->handle to NULL.  If fail for any other
   reason, return the error.

   Allocate *RET and its xlate handle in POOL if svn_utf_initialize()
   hasn't been called or USERDATA_KEY is NULL.  Else, allocate them
   in the pool of xlate_handle_hash. */
static svn_error_t *
get_xlate_handle_node(xlate_handle_node_t **ret,
                      const char *topage, const char *frompage,
                      const char *userdata_key, apr_pool_t *pool)
{
  xlate_handle_node_t *old_node = NULL;

  /* If we already have a handle, just return it. */
  if (userdata_key)
    {
      if (xlate_handle_hash)
        {
          /* 1st level: global, static items */
          if (userdata_key == SVN_UTF_NTOU_XLATE_HANDLE)
            old_node = atomic_swap(&xlat_ntou_static_handle, NULL);
          else if (userdata_key == SVN_UTF_UTON_XLATE_HANDLE)
            old_node = atomic_swap(&xlat_uton_static_handle, NULL);

          if (old_node && old_node->valid)
            {
              *ret = old_node;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          void *p;
          /* We fall back on a per-pool cache instead. */
          apr_pool_userdata_get(&p, userdata_key, pool);
          old_node = p;
          /* Ensure that the handle is still valid. */
          if (old_node && old_node->valid)
            {
              *ret = old_node;
              return SVN_NO_ERROR;
            }

          return xlate_alloc_handle(ret, topage, frompage, pool);
        }
    }

  SVN_MUTEX__WITH_LOCK(xlate_handle_mutex,
                       get_xlate_handle_node_internal(ret,
                                                      topage,
                                                      frompage,
                                                      userdata_key,
                                                      pool));

  return SVN_NO_ERROR;
}

/* Put back NODE into the xlate handle cache for use by other calls.

   Note: this function is not thread-safe. Call put_xlate_handle_node
   instead. */
static svn_error_t *
put_xlate_handle_node_internal(xlate_handle_node_t *node,
                               const char *userdata_key)
{
  xlate_handle_node_t **node_p = svn_hash_gets(xlate_handle_hash, userdata_key);
  if (node_p == NULL)
    {
      userdata_key = apr_pstrdup(apr_hash_pool_get(xlate_handle_hash),
                                  userdata_key);
      node_p = apr_palloc(apr_hash_pool_get(xlate_handle_hash),
                          sizeof(*node_p));
      *node_p = NULL;
      svn_hash_sets(xlate_handle_hash, userdata_key, node_p);
    }
  node->next = *node_p;
  *node_p = node;

  return SVN_NO_ERROR;
}

/* Put back NODE into the xlate handle cache for use by other calls.
   If there is no global cache, store the handle in POOL.
   Ignore errors related to locking/unlocking the mutex. */
static svn_error_t *
put_xlate_handle_node(xlate_handle_node_t *node,
                      const char *userdata_key,
                      apr_pool_t *pool)
{
  assert(node->next == NULL);
  if (!userdata_key)
    return SVN_NO_ERROR;

  /* push previous global node to the hash */
  if (xlate_handle_hash)
    {
      /* 1st level: global, static items */
      if (userdata_key == SVN_UTF_NTOU_XLATE_HANDLE)
        node = atomic_swap(&xlat_ntou_static_handle, node);
      else if (userdata_key == SVN_UTF_UTON_XLATE_HANDLE)
        node = atomic_swap(&xlat_uton_static_handle, node);
      if (node == NULL)
        return SVN_NO_ERROR;

      SVN_MUTEX__WITH_LOCK(xlate_handle_mutex,
                           put_xlate_handle_node_internal(node,
                                                          userdata_key));
    }
  else
    {
      /* Store it in the per-pool cache. */
      apr_pool_userdata_set(node, userdata_key, apr_pool_cleanup_null, pool);
    }

  return SVN_NO_ERROR;
}

/* Return the apr_xlate handle for converting native characters to UTF-8. */
static svn_error_t *
get_ntou_xlate_handle_node(xlate_handle_node_t **ret, apr_pool_t *pool)
{
  return get_xlate_handle_node(ret, SVN_APR_UTF8_CHARSET,
                               assume_native_charset_is_utf8
                                 ? SVN_APR_UTF8_CHARSET
                                 : SVN_APR_LOCALE_CHARSET,
                               SVN_UTF_NTOU_XLATE_HANDLE, pool);
}


/* Return the apr_xlate handle for converting UTF-8 to native characters.
   Create one if it doesn't exist.  If unable to find a handle, or
   unable to create one because apr_xlate_open returned APR_EINVAL, then
   set *RET to null and return SVN_NO_ERROR; if fail for some other
   reason, return error. */
static svn_error_t *
get_uton_xlate_handle_node(xlate_handle_node_t **ret, apr_pool_t *pool)
{
  return get_xlate_handle_node(ret,
                               assume_native_charset_is_utf8
                                 ? SVN_APR_UTF8_CHARSET
                                 : SVN_APR_LOCALE_CHARSET,
                               SVN_APR_UTF8_CHARSET,
                               SVN_UTF_UTON_XLATE_HANDLE, pool);
}


/* Convert SRC_LENGTH bytes of SRC_DATA in NODE->handle, store the result
   in *DEST, which is allocated in POOL. */
static svn_error_t *
convert_to_stringbuf(xlate_handle_node_t *node,
                     const char *src_data,
                     apr_size_t src_length,
                     svn_stringbuf_t **dest,
                     apr_pool_t *pool)
{
#ifdef WIN32
  apr_status_t apr_err;

  apr_err = svn_subr__win32_xlate_to_stringbuf(node->handle, src_data,
                                               src_length, dest, pool);
#else
  apr_size_t buflen = src_length * 2;
  apr_status_t apr_err;
  apr_size_t srclen = src_length;
  apr_size_t destlen = buflen;

  /* Initialize *DEST to an empty stringbuf.
     A 1:2 ratio of input bytes to output bytes (as assigned above)
     should be enough for most translations, and if it turns out not
     to be enough, we'll grow the buffer again, sizing it based on a
     1:3 ratio of the remainder of the string. */
  *dest = svn_stringbuf_create_ensure(buflen + 1, pool);

  /* Not only does it not make sense to convert an empty string, but
     apr-iconv is quite unreasonable about not allowing that. */
  if (src_length == 0)
    return SVN_NO_ERROR;

  do
    {
      /* Set up state variables for xlate. */
      destlen = buflen - (*dest)->len;

      /* Attempt the conversion. */
      apr_err = apr_xlate_conv_buffer(node->handle,
                                      src_data + (src_length - srclen),
                                      &srclen,
                                      (*dest)->data + (*dest)->len,
                                      &destlen);

      /* Now, update the *DEST->len to track the amount of output data
         churned out so far from this loop. */
      (*dest)->len += ((buflen - (*dest)->len) - destlen);
      buflen += srclen * 3; /* 3 is middle ground, 2 wasn't enough
                               for all characters in the buffer, 4 is
                               maximum character size (currently) */


    } while (apr_err == APR_SUCCESS && srclen != 0);
#endif

  /* If we exited the loop with an error, return the error. */
  if (apr_err)
    {
      const char *errstr;
      svn_error_t *err;

      /* Can't use svn_error_wrap_apr here because it calls functions in
         this file, leading to infinite recursion. */
      if (node->frompage == SVN_APR_LOCALE_CHARSET)
        errstr = apr_psprintf
          (pool, _("Can't convert string from native encoding to '%s':"),
           node->topage);
      else if (node->topage == SVN_APR_LOCALE_CHARSET)
        errstr = apr_psprintf
          (pool, _("Can't convert string from '%s' to native encoding:"),
           node->frompage);
      else
        errstr = apr_psprintf
          (pool, _("Can't convert string from '%s' to '%s':"),
           node->frompage, node->topage);

      err = svn_error_create(
          apr_err, NULL, svn_utf__fuzzy_escape(src_data, src_length, pool));
      return svn_error_create(apr_err, err, errstr);
    }
  /* Else, exited due to success.  Trim the result buffer down to the
     right length. */
  (*dest)->data[(*dest)->len] = '\0';

  return SVN_NO_ERROR;
}


/* Return APR_EINVAL if the first LEN bytes of DATA contain anything
   other than seven-bit, non-control (except for whitespace) ASCII
   characters, finding the error pool from POOL.  Otherwise, return
   SVN_NO_ERROR. */
static svn_error_t *
check_non_ascii(const char *data, apr_size_t len, apr_pool_t *pool)
{
  const char *data_start = data;

  for (; len > 0; --len, data++)
    {
      if ((! svn_ctype_isascii(*data))
          || ((! svn_ctype_isspace(*data))
              && svn_ctype_iscntrl(*data)))
        {
          /* Show the printable part of the data, followed by the
             decimal code of the questionable character.  Because if a
             user ever gets this error, she's going to have to spend
             time tracking down the non-ASCII data, so we want to help
             as much as possible.  And yes, we just call the unsafe
             data "non-ASCII", even though the actual constraint is
             somewhat more complex than that. */

          if (data - data_start)
            {
              const char *error_data
                = apr_pstrndup(pool, data_start, (data - data_start));

              return svn_error_createf
                (APR_EINVAL, NULL,
                 _("Safe data '%s' was followed by non-ASCII byte %d: "
                   "unable to convert to/from UTF-8"),
                 error_data, *((const unsigned char *) data));
            }
          else
            {
              return svn_error_createf
                (APR_EINVAL, NULL,
                 _("Non-ASCII character (code %d) detected, "
                   "and unable to convert to/from UTF-8"),
                 *((const unsigned char *) data));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Construct an error with code APR_EINVAL and with a suitable message
 * to describe the invalid UTF-8 sequence DATA of length LEN (which
 * may have embedded NULLs).  We can't simply print the data, almost
 * by definition we don't really know how it is encoded.
 */
static svn_error_t *
invalid_utf8(const char *data, apr_size_t len, apr_pool_t *pool)
{
  const char *last = svn_utf__last_valid(data, len);
  const char *valid_txt = "", *invalid_txt = "";
  apr_size_t i;
  size_t valid, invalid;

  /* We will display at most 24 valid octets (this may split a leading
     multi-byte character) as that should fit on one 80 character line. */
  valid = last - data;
  if (valid > 24)
    valid = 24;
  for (i = 0; i < valid; ++i)
    valid_txt = apr_pstrcat(pool, valid_txt,
                            apr_psprintf(pool, " %02x",
                                         (unsigned char)last[i-valid]),
                                         SVN_VA_NULL);

  /* 4 invalid octets will guarantee that the faulty octet is displayed */
  invalid = data + len - last;
  if (invalid > 4)
    invalid = 4;
  for (i = 0; i < invalid; ++i)
    invalid_txt = apr_pstrcat(pool, invalid_txt,
                              apr_psprintf(pool, " %02x",
                                           (unsigned char)last[i]),
                                           SVN_VA_NULL);

  return svn_error_createf(APR_EINVAL, NULL,
                           _("Valid UTF-8 data\n(hex:%s)\n"
                             "followed by invalid UTF-8 sequence\n(hex:%s)"),
                           valid_txt, invalid_txt);
}

/* Verify that the sequence DATA of length LEN is valid UTF-8.
   If it is not, return an error with code APR_EINVAL. */
static svn_error_t *
check_utf8(const char *data, apr_size_t len, apr_pool_t *pool)
{
  if (! svn_utf__is_valid(data, len))
    return invalid_utf8(data, len, pool);
  return SVN_NO_ERROR;
}

/* Verify that the NULL terminated sequence DATA is valid UTF-8.
   If it is not, return an error with code APR_EINVAL. */
static svn_error_t *
check_cstring_utf8(const char *data, apr_pool_t *pool)
{

  if (! svn_utf__cstring_is_valid(data))
    return invalid_utf8(data, strlen(data), pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf_stringbuf_to_utf8(svn_stringbuf_t **dest,
                          const svn_stringbuf_t *src,
                          apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_ntou_xlate_handle_node(&node, pool));

  if (node->handle)
    {
      err = convert_to_stringbuf(node, src->data, src->len, dest, pool);
      if (! err)
        err = check_utf8((*dest)->data, (*dest)->len, pool);
    }
  else
    {
      err = check_non_ascii(src->data, src->len, pool);
      if (! err)
        *dest = svn_stringbuf_dup(src, pool);
    }

  return svn_error_compose_create(err,
                                  put_xlate_handle_node
                                     (node,
                                      SVN_UTF_NTOU_XLATE_HANDLE,
                                      pool));
}


svn_error_t *
svn_utf_string_to_utf8(const svn_string_t **dest,
                       const svn_string_t *src,
                       apr_pool_t *pool)
{
  svn_stringbuf_t *destbuf;
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_ntou_xlate_handle_node(&node, pool));

  if (node->handle)
    {
      err = convert_to_stringbuf(node, src->data, src->len, &destbuf, pool);
      if (! err)
        err = check_utf8(destbuf->data, destbuf->len, pool);
      if (! err)
        *dest = svn_stringbuf__morph_into_string(destbuf);
    }
  else
    {
      err = check_non_ascii(src->data, src->len, pool);
      if (! err)
        *dest = svn_string_dup(src, pool);
    }

  return svn_error_compose_create(err,
                                  put_xlate_handle_node
                                     (node,
                                      SVN_UTF_NTOU_XLATE_HANDLE,
                                      pool));
}


/* Common implementation for svn_utf_cstring_to_utf8,
   svn_utf_cstring_to_utf8_ex, svn_utf_cstring_from_utf8 and
   svn_utf_cstring_from_utf8_ex. Convert SRC to DEST using NODE->handle as
   the translator and allocating from POOL. */
static svn_error_t *
convert_cstring(const char **dest,
                const char *src,
                xlate_handle_node_t *node,
                apr_pool_t *pool)
{
  if (node->handle)
    {
      svn_stringbuf_t *destbuf;
      SVN_ERR(convert_to_stringbuf(node, src, strlen(src),
                                   &destbuf, pool));
      *dest = destbuf->data;
    }
  else
    {
      apr_size_t len = strlen(src);
      SVN_ERR(check_non_ascii(src, len, pool));
      *dest = apr_pstrmemdup(pool, src, len);
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf_cstring_to_utf8(const char **dest,
                        const char *src,
                        apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_ntou_xlate_handle_node(&node, pool));
  err = convert_cstring(dest, src, node, pool);
  SVN_ERR(svn_error_compose_create(err,
                                   put_xlate_handle_node
                                      (node,
                                       SVN_UTF_NTOU_XLATE_HANDLE,
                                       pool)));
  return check_cstring_utf8(*dest, pool);
}


svn_error_t *
svn_utf_cstring_to_utf8_ex2(const char **dest,
                            const char *src,
                            const char *frompage,
                            apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;
  const char *convset_key = get_xlate_key(SVN_APR_UTF8_CHARSET, frompage,
                                          pool);

  SVN_ERR(get_xlate_handle_node(&node, SVN_APR_UTF8_CHARSET, frompage,
                                convset_key, pool));
  err = convert_cstring(dest, src, node, pool);
  SVN_ERR(svn_error_compose_create(err,
                                   put_xlate_handle_node
                                      (node,
                                       SVN_UTF_NTOU_XLATE_HANDLE,
                                       pool)));

  return check_cstring_utf8(*dest, pool);
}


svn_error_t *
svn_utf_cstring_to_utf8_ex(const char **dest,
                           const char *src,
                           const char *frompage,
                           const char *convset_key,
                           apr_pool_t *pool)
{
  return svn_utf_cstring_to_utf8_ex2(dest, src, frompage, pool);
}


svn_error_t *
svn_utf_stringbuf_from_utf8(svn_stringbuf_t **dest,
                            const svn_stringbuf_t *src,
                            apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_uton_xlate_handle_node(&node, pool));

  if (node->handle)
    {
      err = check_utf8(src->data, src->len, pool);
      if (! err)
        err = convert_to_stringbuf(node, src->data, src->len, dest, pool);
    }
  else
    {
      err = check_non_ascii(src->data, src->len, pool);
      if (! err)
        *dest = svn_stringbuf_dup(src, pool);
    }

  err = svn_error_compose_create(
          err,
          put_xlate_handle_node(node, SVN_UTF_UTON_XLATE_HANDLE, pool));

  return err;
}


svn_error_t *
svn_utf_string_from_utf8(const svn_string_t **dest,
                         const svn_string_t *src,
                         apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_uton_xlate_handle_node(&node, pool));

  if (node->handle)
    {
      err = check_utf8(src->data, src->len, pool);
      if (! err)
        {
          svn_stringbuf_t *dbuf;

          err = convert_to_stringbuf(node, src->data, src->len,
                                     &dbuf, pool);

          if (! err)
            *dest = svn_stringbuf__morph_into_string(dbuf);
        }
    }
  else
    {
      err = check_non_ascii(src->data, src->len, pool);
      if (! err)
        *dest = svn_string_dup(src, pool);
    }

  err = svn_error_compose_create(
          err,
          put_xlate_handle_node(node, SVN_UTF_UTON_XLATE_HANDLE, pool));

  return err;
}


svn_error_t *
svn_utf_cstring_from_utf8(const char **dest,
                          const char *src,
                          apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(check_cstring_utf8(src, pool));

  SVN_ERR(get_uton_xlate_handle_node(&node, pool));
  err = convert_cstring(dest, src, node, pool);
  err = svn_error_compose_create(
          err,
          put_xlate_handle_node(node, SVN_UTF_UTON_XLATE_HANDLE, pool));

  return err;
}


svn_error_t *
svn_utf_cstring_from_utf8_ex2(const char **dest,
                              const char *src,
                              const char *topage,
                              apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;
  const char *convset_key = get_xlate_key(topage, SVN_APR_UTF8_CHARSET,
                                          pool);

  SVN_ERR(check_cstring_utf8(src, pool));

  SVN_ERR(get_xlate_handle_node(&node, topage, SVN_APR_UTF8_CHARSET,
                                convset_key, pool));
  err = convert_cstring(dest, src, node, pool);
  err = svn_error_compose_create(
          err,
          put_xlate_handle_node(node, convset_key, pool));

  return err;
}

const char *
svn_utf__cstring_from_utf8_fuzzy(const char *src,
                                 apr_pool_t *pool,
                                 svn_error_t *(*convert_from_utf8)
                                 (const char **, const char *, apr_pool_t *))
{
  const char *escaped, *converted;
  svn_error_t *err;

  escaped = svn_utf__fuzzy_escape(src, strlen(src), pool);

  /* Okay, now we have a *new* UTF-8 string, one that's guaranteed to
     contain only 7-bit bytes :-).  Recode to native... */
  err = convert_from_utf8(((const char **) &converted), escaped, pool);

  if (err)
    {
      svn_error_clear(err);
      return escaped;
    }
  else
    return converted;

  /* ### Check the client locale, maybe we can avoid that second
   * conversion!  See Ulrich Drepper's patch at
   * http://subversion.tigris.org/issues/show_bug.cgi?id=807.
   */
}


const char *
svn_utf_cstring_from_utf8_fuzzy(const char *src,
                                apr_pool_t *pool)
{
  return svn_utf__cstring_from_utf8_fuzzy(src, pool,
                                          svn_utf_cstring_from_utf8);
}


svn_error_t *
svn_utf_cstring_from_utf8_stringbuf(const char **dest,
                                    const svn_stringbuf_t *src,
                                    apr_pool_t *pool)
{
  svn_stringbuf_t *destbuf;

  SVN_ERR(svn_utf_stringbuf_from_utf8(&destbuf, src, pool));
  *dest = destbuf->data;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf_cstring_from_utf8_string(const char **dest,
                                 const svn_string_t *src,
                                 apr_pool_t *pool)
{
  xlate_handle_node_t *node;
  svn_error_t *err;

  SVN_ERR(get_uton_xlate_handle_node(&node, pool));

  if (node->handle)
    {
      err = check_utf8(src->data, src->len, pool);
      if (! err)
        {
          svn_stringbuf_t *dbuf;

          err = convert_to_stringbuf(node, src->data, src->len,
                                     &dbuf, pool);
          if (! err)
            *dest = dbuf->data;
        }
    }
  else
    {
      err = check_non_ascii(src->data, src->len, pool);
      if (! err)
        *dest = apr_pstrmemdup(pool, src->data, src->len);
    }

  err = svn_error_compose_create(
          err,
          put_xlate_handle_node(node, SVN_UTF_UTON_XLATE_HANDLE, pool));

  return err;
}


/* Insert the given UCS-4 VALUE into BUF at the given OFFSET. */
static void
membuf_insert_ucs4(svn_membuf_t *buf, apr_size_t offset, apr_int32_t value)
{
  svn_membuf__resize(buf, (offset + 1) * sizeof(value));
  ((apr_int32_t*)buf->data)[offset] = value;
}

/* TODO: Use compiler intrinsics for byte swaps. */
#define SWAP_SHORT(x)  ((((x) & 0xff) << 8) | (((x) >> 8) & 0xff))
#define SWAP_LONG(x)   ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8)    \
                        | (((x) >> 8) & 0xff00) | (((x) >> 24) & 0xff))

#define IS_UTF16_LEAD_SURROGATE(c)   ((c) >= 0xd800 && (c) <= 0xdbff)
#define IS_UTF16_TRAIL_SURROGATE(c)  ((c) >= 0xdc00 && (c) <= 0xdfff)

svn_error_t *
svn_utf__utf16_to_utf8(const svn_string_t **result,
                       const apr_uint16_t *utf16str,
                       apr_size_t utf16len,
                       svn_boolean_t big_endian,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  static const apr_uint16_t endiancheck = 0xa55a;
  const svn_boolean_t arch_big_endian =
    (((const char*)&endiancheck)[sizeof(endiancheck) - 1] == '\x5a');
  const svn_boolean_t swap_order = (!big_endian != !arch_big_endian);

  apr_uint16_t lead_surrogate;
  apr_size_t length;
  apr_size_t offset;
  svn_membuf_t ucs4buf;
  svn_membuf_t resultbuf;
  svn_string_t *res;

  if (utf16len == SVN_UTF__UNKNOWN_LENGTH)
    {
      const apr_uint16_t *endp = utf16str;
      while (*endp++)
        ;
      utf16len = (endp - utf16str);
    }

  svn_membuf__create(&ucs4buf, utf16len * sizeof(apr_int32_t), scratch_pool);

  for (lead_surrogate = 0, length = 0, offset = 0;
       offset < utf16len; ++offset)
    {
      const apr_uint16_t code =
        (swap_order ? SWAP_SHORT(utf16str[offset]) : utf16str[offset]);

      if (lead_surrogate)
        {
          if (IS_UTF16_TRAIL_SURROGATE(code))
            {
              /* Combine the lead and trail currogates into a 32-bit code. */
              membuf_insert_ucs4(&ucs4buf, length++,
                                 (0x010000
                                  + (((lead_surrogate & 0x03ff) << 10)
                                     | (code & 0x03ff))));
              lead_surrogate = 0;
              continue;
            }
          else
            {
              /* If we didn't find a surrogate pair, just dump the
                 lead surrogate into the stream. */
              membuf_insert_ucs4(&ucs4buf, length++, lead_surrogate);
              lead_surrogate = 0;
            }
        }

      if ((offset + 1) < utf16len && IS_UTF16_LEAD_SURROGATE(code))
        {
          /* Store a lead surrogate that is followed by at least one
             code for the next iteration. */
          lead_surrogate = code;
          continue;
        }
      else
        membuf_insert_ucs4(&ucs4buf, length++, code);
    }

  /* Convert the UCS-4 buffer to UTF-8, assuming an average of 2 bytes
     per code point for encoding. The buffer will grow as
     necessary. */
  svn_membuf__create(&resultbuf, length * 2, result_pool);
  SVN_ERR(svn_utf__encode_ucs4_string(
              &resultbuf, ucs4buf.data, length, &length));

  res = apr_palloc(result_pool, sizeof(*res));
  res->data = resultbuf.data;
  res->len = length;
  *result = res;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf__utf32_to_utf8(const svn_string_t **result,
                       const apr_int32_t *utf32str,
                       apr_size_t utf32len,
                       svn_boolean_t big_endian,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  static const apr_int32_t endiancheck = 0xa5cbbc5a;
  const svn_boolean_t arch_big_endian =
    (((const char*)&endiancheck)[sizeof(endiancheck) - 1] == '\x5a');
  const svn_boolean_t swap_order = (!big_endian != !arch_big_endian);

  apr_size_t length;
  svn_membuf_t resultbuf;
  svn_string_t *res;

  if (utf32len == SVN_UTF__UNKNOWN_LENGTH)
    {
      const apr_int32_t *endp = utf32str;
      while (*endp++)
        ;
      utf32len = (endp - utf32str);
    }

  if (swap_order)
    {
      apr_size_t offset;
      svn_membuf_t ucs4buf;

      svn_membuf__create(&ucs4buf, utf32len * sizeof(apr_int32_t),
                         scratch_pool);

      for (offset = 0; offset < utf32len; ++offset)
        {
          const apr_int32_t code = SWAP_LONG(utf32str[offset]);
          membuf_insert_ucs4(&ucs4buf, offset, code);
        }
      utf32str = ucs4buf.data;
    }

  /* Convert the UCS-4 buffer to UTF-8, assuming an average of 2 bytes
     per code point for encoding. The buffer will grow as
     necessary. */
  svn_membuf__create(&resultbuf, utf32len * 2, result_pool);
  SVN_ERR(svn_utf__encode_ucs4_string(
              &resultbuf, utf32str, utf32len, &length));

  res = apr_palloc(result_pool, sizeof(*res));
  res->data = resultbuf.data;
  res->len = length;
  *result = res;
  return SVN_NO_ERROR;
}


#ifdef WIN32


svn_error_t *
svn_utf__win32_utf8_to_utf16(const WCHAR **result,
                             const char *src,
                             const WCHAR *prefix,
                             apr_pool_t *result_pool)
{
  const int utf8_count = strlen(src);
  const int prefix_len = (prefix ? lstrlenW(prefix) : 0);
  WCHAR *wide_str;
  int wide_count;

  if (0 == prefix_len + utf8_count)
    {
      *result = L"";
      return SVN_NO_ERROR;
    }

  wide_count = MultiByteToWideChar(CP_UTF8, 0, src, utf8_count, NULL, 0);
  if (wide_count == 0)
    return svn_error_wrap_apr(apr_get_os_error(),
                              _("Conversion to UTF-16 failed"));

  wide_str = apr_palloc(result_pool,
                        (prefix_len + wide_count + 1) * sizeof(*wide_str));
  if (prefix_len)
    memcpy(wide_str, prefix, prefix_len * sizeof(*wide_str));
  if (0 == MultiByteToWideChar(CP_UTF8, 0, src, utf8_count,
                               wide_str + prefix_len, wide_count))
    return svn_error_wrap_apr(apr_get_os_error(),
                              _("Conversion to UTF-16 failed"));

  wide_str[prefix_len + wide_count] = 0;
  *result = wide_str;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_utf__win32_utf16_to_utf8(const char **result,
                             const WCHAR *src,
                             const char *prefix,
                             apr_pool_t *result_pool)
{
  const int wide_count = lstrlenW(src);
  const int prefix_len = (prefix ? strlen(prefix) : 0);
  char *utf8_str;
  int utf8_count;

  if (0 == prefix_len + wide_count)
    {
      *result = "";
      return SVN_NO_ERROR;
    }

  utf8_count = WideCharToMultiByte(CP_UTF8, 0, src, wide_count,
                                   NULL, 0, NULL, FALSE);
  if (utf8_count == 0)
    return svn_error_wrap_apr(apr_get_os_error(),
                              _("Conversion from UTF-16 failed"));

  utf8_str = apr_palloc(result_pool,
                        (prefix_len + utf8_count + 1) * sizeof(*utf8_str));
  if (prefix_len)
    memcpy(utf8_str, prefix, prefix_len * sizeof(*utf8_str));
  if (0 == WideCharToMultiByte(CP_UTF8, 0, src, wide_count,
                               utf8_str + prefix_len, utf8_count,
                               NULL, FALSE))
    return svn_error_wrap_apr(apr_get_os_error(),
                              _("Conversion from UTF-16 failed"));

  utf8_str[prefix_len + utf8_count] = 0;
  *result = utf8_str;

  return SVN_NO_ERROR;
}

#endif /* WIN32 */
