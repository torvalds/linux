/* atomic.c : perform atomic initialization
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

#include <assert.h>
#include <apr_time.h>

#include "svn_pools.h"

#include "private/svn_atomic.h"
#include "private/svn_mutex.h"

/* Magic values for atomic initialization */
#define SVN_ATOMIC_UNINITIALIZED 0
#define SVN_ATOMIC_START_INIT    1
#define SVN_ATOMIC_INIT_FAILED   2
#define SVN_ATOMIC_INITIALIZED   3


/* Baton used by init_funct_t and init_once(). */
typedef struct init_baton_t init_baton_t;

/* Initialization function wrapper. Hides API details from init_once().
   The implementation must return FALSE on failure. */
typedef svn_boolean_t (*init_func_t)(init_baton_t *init_baton);

/*
 * This is the actual atomic initialization driver.
 * Returns FALSE on failure.
 */
static svn_boolean_t
init_once(volatile svn_atomic_t *global_status,
          init_func_t init_func, init_baton_t *init_baton)
{
  /* !! Don't use localizable strings in this function, because these
     !! might cause deadlocks. This function can be used to initialize
     !! libraries that are used for generating error messages. */

  /* We have to call init_func exactly once.  Because APR
     doesn't have statically-initialized mutexes, we implement a poor
     man's spinlock using svn_atomic_cas. */

  svn_atomic_t status = svn_atomic_cas(global_status,
                                       SVN_ATOMIC_START_INIT,
                                       SVN_ATOMIC_UNINITIALIZED);

  for (;;)
    {
      switch (status)
        {
        case SVN_ATOMIC_UNINITIALIZED:
          {
            const svn_boolean_t result = init_func(init_baton);
            const svn_atomic_t init_state = (result
                                             ? SVN_ATOMIC_INITIALIZED
                                             : SVN_ATOMIC_INIT_FAILED);

            svn_atomic_cas(global_status, init_state,
                           SVN_ATOMIC_START_INIT);
            return result;
          }

        case SVN_ATOMIC_START_INIT:
          /* Wait for the init function to complete. */
          apr_sleep(APR_USEC_PER_SEC / 1000);
          status = svn_atomic_cas(global_status,
                                  SVN_ATOMIC_UNINITIALIZED,
                                  SVN_ATOMIC_UNINITIALIZED);
          continue;

        case SVN_ATOMIC_INIT_FAILED:
          return FALSE;

        case SVN_ATOMIC_INITIALIZED:
          return TRUE;

        default:
          /* Something went seriously wrong with the atomic operations. */
          abort();
        }
    }
}


/* This baton structure is used by the two flavours of init-once APIs
   to hide their differences from the init_once() driver. Each private
   API uses only selected parts of the baton.

   No part of this structure changes unless a wrapped init function is
   actually invoked by init_once().
*/
struct init_baton_t
{
  /* Used only by svn_atomic__init_once()/err_init_func_wrapper() */
  svn_atomic__err_init_func_t err_init_func;
  svn_error_t *err;
  apr_pool_t *pool;

  /* Used only by svn_atomic__init_no_error()/str_init_func_wrapper() */
  svn_atomic__str_init_func_t str_init_func;
  const char *errstr;

  /* Used by both pairs of functions */
  void *baton;
};

/* Wrapper for the svn_atomic__init_once init function. */
static svn_boolean_t err_init_func_wrapper(init_baton_t *init_baton)
{
  init_baton->err = init_baton->err_init_func(init_baton->baton,
                                              init_baton->pool);
  return (init_baton->err == SVN_NO_ERROR);
}

svn_error_t *
svn_atomic__init_once(volatile svn_atomic_t *global_status,
                      svn_atomic__err_init_func_t err_init_func,
                      void *baton,
                      apr_pool_t* pool)
{
  init_baton_t init_baton;
  init_baton.err_init_func = err_init_func;
  init_baton.err = NULL;
  init_baton.pool = pool;
  init_baton.baton = baton;

  if (init_once(global_status, err_init_func_wrapper, &init_baton))
    return SVN_NO_ERROR;

  return svn_error_create(SVN_ERR_ATOMIC_INIT_FAILURE, init_baton.err,
                          "Couldn't perform atomic initialization");
}


/* Wrapper for the svn_atomic__init_no_error init function. */
static svn_boolean_t str_init_func_wrapper(init_baton_t *init_baton)
{
  init_baton->errstr = init_baton->str_init_func(init_baton->baton);
  return (init_baton->errstr == NULL);
}

const char *
svn_atomic__init_once_no_error(volatile svn_atomic_t *global_status,
                               svn_atomic__str_init_func_t str_init_func,
                               void *baton)
{
  init_baton_t init_baton;
  init_baton.str_init_func = str_init_func;
  init_baton.errstr = NULL;
  init_baton.baton = baton;

  if (init_once(global_status, str_init_func_wrapper, &init_baton))
    return NULL;

  /* Our init function wrapper may not have been called; make sure
     that we return generic error message in that case. */
  if (!init_baton.errstr)
    return "Couldn't perform atomic initialization";
  else
    return init_baton.errstr;
}

/* The process-global counter that we use to produce process-wide unique
 * values.  Since APR has no 64 bit atomics, all access to this will be
 * serialized through COUNTER_MUTEX. */
static apr_uint64_t uniqiue_counter = 0;

/* The corresponding mutex and initialization state. */
static volatile svn_atomic_t counter_status = SVN_ATOMIC_UNINITIALIZED;
static svn_mutex__t *counter_mutex = NULL;

/* svn_atomic__err_init_func_t implementation that initializes COUNTER_MUTEX.
 * Note that neither argument will be used and should be NULL. */
static svn_error_t *
init_unique_counter(void *null_baton,
                    apr_pool_t *null_pool)
{
  /* COUNTER_MUTEX is global, so it needs to live in a global pool.
   * APR also makes those thread-safe by default. */
  SVN_ERR(svn_mutex__init(&counter_mutex, TRUE, svn_pool_create(NULL)));
  return SVN_NO_ERROR;
}

/* Read and increment UNIQIUE_COUNTER. Return the new value in *VALUE.
 * Call this function only while having acquired the COUNTER_MUTEX. */
static svn_error_t *
read_unique_counter(apr_uint64_t *value)
{
  *value = ++uniqiue_counter;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_atomic__unique_counter(apr_uint64_t *value)
{
  SVN_ERR(svn_atomic__init_once(&counter_status, init_unique_counter, NULL,
                                NULL));
  SVN_MUTEX__WITH_LOCK(counter_mutex, read_unique_counter(value));
  return SVN_NO_ERROR;
}
