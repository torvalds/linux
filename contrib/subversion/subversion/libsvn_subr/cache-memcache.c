/*
 * cache-memcache.c: memcached caching for Subversion
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

#include <apr_md5.h>

#include "svn_pools.h"
#include "svn_base64.h"
#include "svn_path.h"

#include "svn_private_config.h"
#include "private/svn_cache.h"
#include "private/svn_dep_compat.h"

#include "cache.h"

#ifdef SVN_HAVE_MEMCACHE

#include <apr_memcache.h>

/* A note on thread safety:

   The apr_memcache_t object does its own mutex handling, and nothing
   else in memcache_t is ever modified, so this implementation should
   be fully thread-safe.
*/

/* The (internal) cache object. */
typedef struct memcache_t {
  /* The memcached server set we're using. */
  apr_memcache_t *memcache;

  /* A prefix used to differentiate our data from any other data in
   * the memcached (URI-encoded). */
  const char *prefix;

  /* The size of the key: either a fixed number of bytes or
   * APR_HASH_KEY_STRING. */
  apr_ssize_t klen;


  /* Used to marshal values in and out of the cache. */
  svn_cache__serialize_func_t serialize_func;
  svn_cache__deserialize_func_t deserialize_func;
} memcache_t;

/* The wrapper around apr_memcache_t. */
struct svn_memcache_t {
  apr_memcache_t *c;
};


/* The memcached protocol says the maximum key length is 250.  Let's
   just say 249, to be safe. */
#define MAX_MEMCACHED_KEY_LEN 249
#define MEMCACHED_KEY_UNHASHED_LEN (MAX_MEMCACHED_KEY_LEN - \
                                    2 * APR_MD5_DIGESTSIZE)


/* Set *MC_KEY to a memcache key for the given key KEY for CACHE, allocated
   in POOL. */
static svn_error_t *
build_key(const char **mc_key,
          memcache_t *cache,
          const void *raw_key,
          apr_pool_t *pool)
{
  const char *encoded_suffix;
  const char *long_key;
  apr_size_t long_key_len;

  if (cache->klen == APR_HASH_KEY_STRING)
    encoded_suffix = svn_path_uri_encode(raw_key, pool);
  else
    {
      const svn_string_t *raw = svn_string_ncreate(raw_key, cache->klen, pool);
      const svn_string_t *encoded = svn_base64_encode_string2(raw, FALSE,
                                                              pool);
      encoded_suffix = encoded->data;
    }

  long_key = apr_pstrcat(pool, "SVN:", cache->prefix, ":", encoded_suffix,
                         SVN_VA_NULL);
  long_key_len = strlen(long_key);

  /* We don't want to have a key that's too big.  If it was going to
     be too big, we MD5 the entire string, then replace the last bit
     with the checksum.  Note that APR_MD5_DIGESTSIZE is for the pure
     binary digest; we have to double that when we convert to hex.

     Every key we use will either be at most
     MEMCACHED_KEY_UNHASHED_LEN bytes long, or be exactly
     MAX_MEMCACHED_KEY_LEN bytes long. */
  if (long_key_len > MEMCACHED_KEY_UNHASHED_LEN)
    {
      svn_checksum_t *checksum;
      SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, long_key, long_key_len,
                           pool));

      long_key = apr_pstrcat(pool,
                             apr_pstrmemdup(pool, long_key,
                                            MEMCACHED_KEY_UNHASHED_LEN),
                             svn_checksum_to_cstring_display(checksum, pool),
                             SVN_VA_NULL);
    }

  *mc_key = long_key;
  return SVN_NO_ERROR;
}

/* Core functionality of our getter functions: fetch DATA from the memcached
 * given by CACHE_VOID and identified by KEY. Indicate success in FOUND and
 * use a tempoary sub-pool of POOL for allocations.
 */
static svn_error_t *
memcache_internal_get(char **data,
                      apr_size_t *size,
                      svn_boolean_t *found,
                      void *cache_void,
                      const void *key,
                      apr_pool_t *pool)
{
  memcache_t *cache = cache_void;
  apr_status_t apr_err;
  const char *mc_key;
  apr_pool_t *subpool;

  if (key == NULL)
    {
      *found = FALSE;
      return SVN_NO_ERROR;
    }

  subpool = svn_pool_create(pool);
  SVN_ERR(build_key(&mc_key, cache, key, subpool));

  apr_err = apr_memcache_getp(cache->memcache,
                              pool,
                              mc_key,
                              data,
                              size,
                              NULL /* ignore flags */);
  if (apr_err == APR_NOTFOUND)
    {
      *found = FALSE;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }
  else if (apr_err != APR_SUCCESS || !*data)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown memcached error while reading"));

  *found = TRUE;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
memcache_get(void **value_p,
             svn_boolean_t *found,
             void *cache_void,
             const void *key,
             apr_pool_t *result_pool)
{
  memcache_t *cache = cache_void;
  char *data;
  apr_size_t data_len;
  SVN_ERR(memcache_internal_get(&data,
                                &data_len,
                                found,
                                cache_void,
                                key,
                                result_pool));

  /* If we found it, de-serialize it. */
  if (*found)
    {
      if (cache->deserialize_func)
        {
          SVN_ERR((cache->deserialize_func)(value_p, data, data_len,
                                            result_pool));
        }
      else
        {
          svn_stringbuf_t *value = svn_stringbuf_create_empty(result_pool);
          value->data = data;
          value->blocksize = data_len;
          value->len = data_len - 1; /* account for trailing NUL */
          *value_p = value;
        }
    }

  return SVN_NO_ERROR;
}

/* Implement vtable.has_key in terms of the getter.
 */
static svn_error_t *
memcache_has_key(svn_boolean_t *found,
                 void *cache_void,
                 const void *key,
                 apr_pool_t *scratch_pool)
{
  char *data;
  apr_size_t data_len;
  SVN_ERR(memcache_internal_get(&data,
                                &data_len,
                                found,
                                cache_void,
                                key,
                                scratch_pool));

  return SVN_NO_ERROR;
}

/* Core functionality of our setter functions: store LENGH bytes of DATA
 * to be identified by KEY in the memcached given by CACHE_VOID. Use POOL
 * for temporary allocations.
 */
static svn_error_t *
memcache_internal_set(void *cache_void,
                      const void *key,
                      const char *data,
                      apr_size_t len,
                      apr_pool_t *scratch_pool)
{
  memcache_t *cache = cache_void;
  const char *mc_key;
  apr_status_t apr_err;

  SVN_ERR(build_key(&mc_key, cache, key, scratch_pool));
  apr_err = apr_memcache_set(cache->memcache, mc_key, (char *)data, len, 0, 0);

  /* ### Maybe write failures should be ignored (but logged)? */
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown memcached error while writing"));

  return SVN_NO_ERROR;
}


static svn_error_t *
memcache_set(void *cache_void,
             const void *key,
             void *value,
             apr_pool_t *scratch_pool)
{
  memcache_t *cache = cache_void;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  void *data;
  apr_size_t data_len;
  svn_error_t *err;

  if (key == NULL)
    return SVN_NO_ERROR;

  if (cache->serialize_func)
    {
      SVN_ERR((cache->serialize_func)(&data, &data_len, value, subpool));
    }
  else
    {
      svn_stringbuf_t *value_str = value;
      data = value_str->data;
      data_len = value_str->len + 1; /* copy trailing NUL */
    }

  err = memcache_internal_set(cache_void, key, data, data_len, subpool);

  svn_pool_destroy(subpool);
  return err;
}

static svn_error_t *
memcache_get_partial(void **value_p,
                     svn_boolean_t *found,
                     void *cache_void,
                     const void *key,
                     svn_cache__partial_getter_func_t func,
                     void *baton,
                     apr_pool_t *result_pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  char *data;
  apr_size_t size;
  SVN_ERR(memcache_internal_get(&data,
                                &size,
                                found,
                                cache_void,
                                key,
                                result_pool));

  /* If we found it, de-serialize it. */
  return *found
    ? func(value_p, data, size, baton, result_pool)
    : err;
}


static svn_error_t *
memcache_set_partial(void *cache_void,
                     const void *key,
                     svn_cache__partial_setter_func_t func,
                     void *baton,
                     apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  void *data;
  apr_size_t size;
  svn_boolean_t found = FALSE;

  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  SVN_ERR(memcache_internal_get((char **)&data,
                                &size,
                                &found,
                                cache_void,
                                key,
                                subpool));

  /* If we found it, modify it and write it back to cache */
  if (found)
    {
      SVN_ERR(func(&data, &size, baton, subpool));
      err = memcache_internal_set(cache_void, key, data, size, subpool);
    }

  svn_pool_destroy(subpool);
  return err;
}


static svn_error_t *
memcache_iter(svn_boolean_t *completed,
              void *cache_void,
              svn_iter_apr_hash_cb_t user_cb,
              void *user_baton,
              apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          _("Can't iterate a memcached cache"));
}

static svn_boolean_t
memcache_is_cachable(void *unused, apr_size_t size)
{
  SVN_UNUSED(unused);

  /* The memcached cutoff seems to be a bit (header length?) under a megabyte.
   * We round down a little to be safe.
   */
  return size < 1000000;
}

static svn_error_t *
memcache_get_info(void *cache_void,
                  svn_cache__info_t *info,
                  svn_boolean_t reset,
                  apr_pool_t *result_pool)
{
  memcache_t *cache = cache_void;

  info->id = apr_pstrdup(result_pool, cache->prefix);

  /* we don't have any memory allocation info */

  return SVN_NO_ERROR;
}

static svn_cache__vtable_t memcache_vtable = {
  memcache_get,
  memcache_has_key,
  memcache_set,
  memcache_iter,
  memcache_is_cachable,
  memcache_get_partial,
  memcache_set_partial,
  memcache_get_info
};

svn_error_t *
svn_cache__create_memcache(svn_cache__t **cache_p,
                          svn_memcache_t *memcache,
                          svn_cache__serialize_func_t serialize_func,
                          svn_cache__deserialize_func_t deserialize_func,
                          apr_ssize_t klen,
                          const char *prefix,
                          apr_pool_t *pool)
{
  svn_cache__t *wrapper = apr_pcalloc(pool, sizeof(*wrapper));
  memcache_t *cache = apr_pcalloc(pool, sizeof(*cache));

  cache->serialize_func = serialize_func;
  cache->deserialize_func = deserialize_func;
  cache->klen = klen;
  cache->prefix = svn_path_uri_encode(prefix, pool);
  cache->memcache = memcache->c;

  wrapper->vtable = &memcache_vtable;
  wrapper->cache_internal = cache;
  wrapper->error_handler = 0;
  wrapper->error_baton = 0;
  wrapper->pretend_empty = !!getenv("SVN_X_DOES_NOT_MARK_THE_SPOT");

  *cache_p = wrapper;
  return SVN_NO_ERROR;
}


/*** Creating apr_memcache_t from svn_config_t. ***/

/* Baton for add_memcache_server. */
struct ams_baton {
  apr_memcache_t *memcache;
  apr_pool_t *memcache_pool;
  svn_error_t *err;
};

/* Implements svn_config_enumerator2_t. */
static svn_boolean_t
add_memcache_server(const char *name,
                    const char *value,
                    void *baton,
                    apr_pool_t *pool)
{
  struct ams_baton *b = baton;
  char *host, *scope;
  apr_port_t port;
  apr_status_t apr_err;
  apr_memcache_server_t *server;

  apr_err = apr_parse_addr_port(&host, &scope, &port,
                                value, pool);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Error parsing memcache server '%s'"),
                                  name);
      return FALSE;
    }

  if (scope)
    {
      b->err = svn_error_createf(SVN_ERR_BAD_SERVER_SPECIFICATION, NULL,
                                  _("Scope not allowed in memcache server "
                                    "'%s'"),
                                  name);
      return FALSE;
    }
  if (!host || !port)
    {
      b->err = svn_error_createf(SVN_ERR_BAD_SERVER_SPECIFICATION, NULL,
                                  _("Must specify host and port for memcache "
                                    "server '%s'"),
                                  name);
      return FALSE;
    }

  /* Note: the four numbers here are only relevant when an
     apr_memcache_t is being shared by multiple threads. */
  apr_err = apr_memcache_server_create(b->memcache_pool,
                                       host,
                                       port,
                                       0,  /* min connections */
                                       5,  /* soft max connections */
                                       10, /* hard max connections */
                                       /*  time to live (in microseconds) */
                                       apr_time_from_sec(50),
                                       &server);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Unknown error creating memcache server"));
      return FALSE;
    }

  apr_err = apr_memcache_add_server(b->memcache, server);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Unknown error adding server to memcache"));
      return FALSE;
    }

  return TRUE;
}

#else /* ! SVN_HAVE_MEMCACHE */

/* Stubs for no apr memcache library. */

struct svn_memcache_t {
  void *unused; /* Let's not have a size-zero struct. */
};

svn_error_t *
svn_cache__create_memcache(svn_cache__t **cache_p,
                          svn_memcache_t *memcache,
                          svn_cache__serialize_func_t serialize_func,
                          svn_cache__deserialize_func_t deserialize_func,
                          apr_ssize_t klen,
                          const char *prefix,
                          apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_NO_APR_MEMCACHE, NULL, NULL);
}

#endif /* SVN_HAVE_MEMCACHE */

/* Implements svn_config_enumerator2_t.  Just used for the
   entry-counting return value of svn_config_enumerate2. */
static svn_boolean_t
nop_enumerator(const char *name,
               const char *value,
               void *baton,
               apr_pool_t *pool)
{
  return TRUE;
}

svn_error_t *
svn_cache__make_memcache_from_config(svn_memcache_t **memcache_p,
                                    svn_config_t *config,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  int server_count =
    svn_config_enumerate2(config,
                          SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS,
                          nop_enumerator, NULL, scratch_pool);

  if (server_count == 0)
    {
      *memcache_p = NULL;
      return SVN_NO_ERROR;
    }

  if (server_count > APR_INT16_MAX)
    return svn_error_create(SVN_ERR_TOO_MANY_MEMCACHED_SERVERS, NULL, NULL);

#ifdef SVN_HAVE_MEMCACHE
  {
    struct ams_baton b;
    svn_memcache_t *memcache = apr_pcalloc(result_pool, sizeof(*memcache));
    apr_status_t apr_err = apr_memcache_create(result_pool,
                                               (apr_uint16_t)server_count,
                                               0, /* flags */
                                               &(memcache->c));
    if (apr_err != APR_SUCCESS)
      return svn_error_wrap_apr(apr_err,
                                _("Unknown error creating apr_memcache_t"));

    b.memcache = memcache->c;
    b.memcache_pool = result_pool;
    b.err = SVN_NO_ERROR;
    svn_config_enumerate2(config,
                          SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS,
                          add_memcache_server, &b,
                          scratch_pool);

    if (b.err)
      return b.err;

    *memcache_p = memcache;

    return SVN_NO_ERROR;
  }
#else /* ! SVN_HAVE_MEMCACHE */
  {
    return svn_error_create(SVN_ERR_NO_APR_MEMCACHE, NULL, NULL);
  }
#endif /* SVN_HAVE_MEMCACHE */
}
