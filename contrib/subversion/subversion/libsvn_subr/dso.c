/*
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

#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_dso.h"
#include "svn_pools.h"
#include "svn_private_config.h"

#include "private/svn_mutex.h"
#include "private/svn_atomic.h"
#include "private/svn_subr_private.h"

/* A mutex to protect our global pool and cache. */
static svn_mutex__t *dso_mutex = NULL;

/* Global pool to allocate DSOs in. */
static apr_pool_t *dso_pool;

/* Global cache for storing DSO objects. */
static apr_hash_t *dso_cache;

/* Just an arbitrary location in memory... */
static int not_there_sentinel;

static volatile svn_atomic_t atomic_init_status = 0;

/* A specific value we store in the dso_cache to indicate that the
   library wasn't found.  This keeps us from allocating extra memory
   from dso_pool when trying to find libraries we already know aren't
   there.  */
#define NOT_THERE ((void *) &not_there_sentinel)

static svn_error_t *
atomic_init_func(void *baton,
                 apr_pool_t *pool)
{
  dso_pool = svn_pool_create(NULL);

  SVN_ERR(svn_mutex__init(&dso_mutex, TRUE, dso_pool));

  dso_cache = apr_hash_make(dso_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_dso_initialize2(void)
{
  SVN_ERR(svn_atomic__init_once(&atomic_init_status, atomic_init_func,
                                NULL, NULL));

  return SVN_NO_ERROR;
}

#if APR_HAS_DSO
static svn_error_t *
svn_dso_load_internal(apr_dso_handle_t **dso, const char *fname)
{
  *dso = svn_hash_gets(dso_cache, fname);

  /* First check to see if we've been through this before...  We do this
     to avoid calling apr_dso_load multiple times for a given library,
     which would result in wasting small amounts of memory each time. */
  if (*dso == NOT_THERE)
    {
      *dso = NULL;
      return SVN_NO_ERROR;
    }

  /* If we got nothing back from the cache, try and load the library. */
  if (! *dso)
    {
      apr_status_t status = apr_dso_load(dso, fname, dso_pool);
      if (status)
        {
#ifdef SVN_DEBUG_DSO
          char buf[1024];
          fprintf(stderr,
                  "Dynamic loading of '%s' failed with the following error:\n%s\n",
                  fname,
                  apr_dso_error(*dso, buf, 1024));
#endif
          *dso = NULL;

          /* It wasn't found, so set the special "we didn't find it" value. */
          svn_hash_sets(dso_cache, apr_pstrdup(dso_pool, fname), NOT_THERE);

          return SVN_NO_ERROR;
        }

      /* Stash the dso so we can use it next time. */
      svn_hash_sets(dso_cache, apr_pstrdup(dso_pool, fname), *dso);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_dso_load(apr_dso_handle_t **dso, const char *fname)
{
  SVN_ERR(svn_dso_initialize2());

  SVN_MUTEX__WITH_LOCK(dso_mutex, svn_dso_load_internal(dso, fname));

  return SVN_NO_ERROR;
}

apr_pool_t *
svn_dso__pool(void)
{
  return dso_pool;
}

#endif /* APR_HAS_DSO */
