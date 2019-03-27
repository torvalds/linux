/* iter.c : iteration drivers
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


#include "svn_iter.h"
#include "svn_pools.h"
#include "private/svn_dep_compat.h"

#include "svn_error_codes.h"

static svn_error_t internal_break_error =
  {
    SVN_ERR_ITER_BREAK, /* APR status */
    NULL, /* message */
    NULL, /* child error */
    NULL, /* pool */
    __FILE__, /* file name */
    __LINE__ /* line number */
  };

#if APR_VERSION_AT_LEAST(1, 4, 0)
struct hash_do_baton
{
  void *baton;
  svn_iter_apr_hash_cb_t func;
  svn_error_t *err;
  apr_pool_t *iterpool;
};

static
int hash_do_callback(void *baton,
                     const void *key,
                     apr_ssize_t klen,
                     const void *value)
{
  struct hash_do_baton *hdb = baton;

  svn_pool_clear(hdb->iterpool);
  hdb->err = (*hdb->func)(hdb->baton, key, klen, (void *)value, hdb->iterpool);

  return hdb->err == SVN_NO_ERROR;
}
#endif

svn_error_t *
svn_iter_apr_hash(svn_boolean_t *completed,
                  apr_hash_t *hash,
                  svn_iter_apr_hash_cb_t func,
                  void *baton,
                  apr_pool_t *pool)
{
#if APR_VERSION_AT_LEAST(1, 4, 0)
  struct hash_do_baton hdb;
  svn_boolean_t error_received;

  hdb.func = func;
  hdb.baton = baton;
  hdb.iterpool = svn_pool_create(pool);

  error_received = !apr_hash_do(hash_do_callback, &hdb, hash);

  svn_pool_destroy(hdb.iterpool);

  if (completed)
    *completed = !error_received;

  if (!error_received)
    return SVN_NO_ERROR;

  if (hdb.err->apr_err == SVN_ERR_ITER_BREAK
        && hdb.err != &internal_break_error)
    {
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(hdb.err);

        hdb.err = SVN_NO_ERROR;
    }

  return hdb.err;
#else
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, hash);
       ! err && hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_ssize_t len;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &len, &val);
      err = (*func)(baton, key, len, val, iterpool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
#endif
}

svn_error_t *
svn_iter_apr_array(svn_boolean_t *completed,
                   const apr_array_header_t *array,
                   svn_iter_apr_array_cb_t func,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  for (i = 0; (! err) && i < array->nelts; ++i)
    {
      void *item = array->elts + array->elt_size*i;

      svn_pool_clear(iterpool);

      err = (*func)(baton, item, iterpool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
}

/* Note: Although this is a "__" function, it is in the public ABI, so
 * we can never remove it or change its signature. */
svn_error_t *
svn_iter__break(void)
{
  return &internal_break_error;
}

#if !APR_VERSION_AT_LEAST(1, 5, 0)
const void *apr_hash_this_key(apr_hash_index_t *hi)
{
  const void *key;

  apr_hash_this((apr_hash_index_t *)hi, &key, NULL, NULL);
  return key;
}

apr_ssize_t apr_hash_this_key_len(apr_hash_index_t *hi)
{
  apr_ssize_t klen;

  apr_hash_this((apr_hash_index_t *)hi, NULL, &klen, NULL);
  return klen;
}

void *apr_hash_this_val(apr_hash_index_t *hi)
{
  void *val;

  apr_hash_this((apr_hash_index_t *)hi, NULL, NULL, &val);
  return val;
}
#endif
