/* batch_fsync.c --- efficiently fsync multiple targets
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

#include <apr_thread_pool.h>
#include <apr_thread_cond.h>

#include "batch_fsync.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_private_config.h"

#include "private/svn_atomic.h"
#include "private/svn_dep_compat.h"
#include "private/svn_mutex.h"
#include "private/svn_subr_private.h"

/* Handy macro to check APR function results and turning them into
 * svn_error_t upon failure. */
#define WRAP_APR_ERR(x,msg)                     \
  {                                             \
    apr_status_t status_ = (x);                 \
    if (status_)                                \
      return svn_error_wrap_apr(status_, msg);  \
  }


/* A simple SVN-wrapper around the apr_thread_cond_* API */
#if APR_HAS_THREADS
typedef apr_thread_cond_t svn_thread_cond__t;
#else
typedef int svn_thread_cond__t;
#endif

static svn_error_t *
svn_thread_cond__create(svn_thread_cond__t **cond,
                        apr_pool_t *result_pool)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_create(cond, result_pool),
               _("Can't create condition variable"));

#else

  *cond = apr_pcalloc(result_pool, sizeof(**cond));

#endif

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_thread_cond__broadcast(svn_thread_cond__t *cond)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_broadcast(cond),
               _("Can't broadcast condition variable"));

#endif

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_thread_cond__wait(svn_thread_cond__t *cond,
                      svn_mutex__t *mutex)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_wait(cond, svn_mutex__get(mutex)),
               _("Can't broadcast condition variable"));

#endif

  return SVN_NO_ERROR;
}

/* Utility construct:  Clients can efficiently wait for the encapsulated
 * counter to reach a certain value.  Currently, only increments have been
 * implemented.  This whole structure can be opaque to the API users.
 */
typedef struct waitable_counter_t
{
  /* Current value, initialized to 0. */
  int value;

  /* Synchronization objects. */
  svn_thread_cond__t *cond;
  svn_mutex__t *mutex;
} waitable_counter_t;

/* Set *COUNTER_P to a new waitable_counter_t instance allocated in
 * RESULT_POOL.  The initial counter value is 0. */
static svn_error_t *
waitable_counter__create(waitable_counter_t **counter_p,
                         apr_pool_t *result_pool)
{
  waitable_counter_t *counter = apr_pcalloc(result_pool, sizeof(*counter));
  counter->value = 0;

  SVN_ERR(svn_thread_cond__create(&counter->cond, result_pool));
  SVN_ERR(svn_mutex__init(&counter->mutex, TRUE, result_pool));

  *counter_p = counter;

  return SVN_NO_ERROR;
}

/* Increment the value in COUNTER by 1. */
static svn_error_t *
waitable_counter__increment(waitable_counter_t *counter)
{
  SVN_ERR(svn_mutex__lock(counter->mutex));
  counter->value++;

  SVN_ERR(svn_thread_cond__broadcast(counter->cond));
  SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));

  return SVN_NO_ERROR;
}

/* Efficiently wait for COUNTER to assume VALUE. */
static svn_error_t *
waitable_counter__wait_for(waitable_counter_t *counter,
                           int value)
{
  svn_boolean_t done = FALSE;

  /* This loop implicitly handles spurious wake-ups. */
  do
    {
      SVN_ERR(svn_mutex__lock(counter->mutex));

      if (counter->value == value)
        done = TRUE;
      else
        SVN_ERR(svn_thread_cond__wait(counter->cond, counter->mutex));

      SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));
    }
  while (!done);

  return SVN_NO_ERROR;
}

/* Set the value in COUNTER to 0. */
static svn_error_t *
waitable_counter__reset(waitable_counter_t *counter)
{
  SVN_ERR(svn_mutex__lock(counter->mutex));
  counter->value = 0;
  SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));

  SVN_ERR(svn_thread_cond__broadcast(counter->cond));

  return SVN_NO_ERROR;
}

/* Entry type for the svn_fs_x__batch_fsync_t collection.  There is one
 * instance per file handle.
 */
typedef struct to_sync_t
{
  /* Open handle of the file / directory to fsync. */
  apr_file_t *file;

  /* Pool to use with FILE.  It is private to FILE such that it can be
   * used safely together with FILE in a separate thread. */
  apr_pool_t *pool;

  /* Result of the file operations. */
  svn_error_t *result;

  /* Counter to increment when we completed the task. */
  waitable_counter_t *counter;
} to_sync_t;

/* The actual collection object. */
struct svn_fs_x__batch_fsync_t
{
  /* Maps open file handles: C-string path to to_sync_t *. */
  apr_hash_t *files;

  /* Counts the number of completed fsync tasks. */
  waitable_counter_t *counter;

  /* Perform fsyncs only if this flag has been set. */
  svn_boolean_t flush_to_disk;
};

/* Data structures for concurrent fsync execution are only available if
 * we have threading support.
 */
#if APR_HAS_THREADS

/* Number of microseconds that an unused thread remains in the pool before
 * being terminated.
 *
 * Higher values are useful if clients frequently send small requests and
 * you want to minimize the latency for those.
 */
#define THREADPOOL_THREAD_IDLE_LIMIT 1000000

/* Maximum number of threads in THREAD_POOL, i.e. number of paths we can
 * fsync concurrently throughout the process. */
#define MAX_THREADS 16

/* Thread pool to execute the fsync tasks. */
static apr_thread_pool_t *thread_pool = NULL;

#endif

/* Keep track on whether we already created the THREAD_POOL . */
static svn_atomic_t thread_pool_initialized = FALSE;

/* We open non-directory files with these flags. */
#define FILE_FLAGS (APR_READ | APR_WRITE | APR_BUFFERED | APR_CREATE)

#if APR_HAS_THREADS

/* Destructor function that implicitly cleans up any running threads
   in the TRHEAD_POOL *once*.

   Must be run as a pre-cleanup hook.
 */
static apr_status_t
thread_pool_pre_cleanup(void *data)
{
  apr_thread_pool_t *tp = thread_pool;
  if (!thread_pool)
    return APR_SUCCESS;

  thread_pool = NULL;
  thread_pool_initialized = FALSE;

  return apr_thread_pool_destroy(tp);
}

#endif

/* Core implementation of svn_fs_x__batch_fsync_init. */
static svn_error_t *
create_thread_pool(void *baton,
                   apr_pool_t *owning_pool)
{
#if APR_HAS_THREADS
  /* The thread-pool must be allocated from a thread-safe pool.
     GLOBAL_POOL may be single-threaded, though. */
  apr_pool_t *pool = svn_pool_create(NULL);

  /* This thread pool will get cleaned up automatically when GLOBAL_POOL
     gets cleared.  No additional cleanup callback is needed. */
  WRAP_APR_ERR(apr_thread_pool_create(&thread_pool, 0, MAX_THREADS, pool),
               _("Can't create fsync thread pool in FSX"));

  /* Work around an APR bug:  The cleanup must happen in the pre-cleanup
     hook instead of the normal cleanup hook.  Otherwise, the sub-pools
     containing the thread objects would already be invalid. */
  apr_pool_pre_cleanup_register(pool, NULL, thread_pool_pre_cleanup);
  apr_pool_pre_cleanup_register(owning_pool, NULL, thread_pool_pre_cleanup);

  /* let idle threads linger for a while in case more requests are
     coming in */
  apr_thread_pool_idle_wait_set(thread_pool, THREADPOOL_THREAD_IDLE_LIMIT);

  /* don't queue requests unless we reached the worker thread limit */
  apr_thread_pool_threshold_set(thread_pool, 0);

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__batch_fsync_init(apr_pool_t *owning_pool)
{
  /* Protect against multiple calls. */
  return svn_error_trace(svn_atomic__init_once(&thread_pool_initialized,
                                               create_thread_pool,
                                               NULL, owning_pool));
}

/* Destructor for svn_fs_x__batch_fsync_t.  Releases all global pool memory
 * and closes all open file handles. */
static apr_status_t
fsync_batch_cleanup(void *data)
{
  svn_fs_x__batch_fsync_t *batch = data;
  apr_hash_index_t *hi;

  /* Close all files (implicitly) and release memory. */
  for (hi = apr_hash_first(apr_hash_pool_get(batch->files), batch->files);
       hi;
       hi = apr_hash_next(hi))
    {
      to_sync_t *to_sync = apr_hash_this_val(hi);
      svn_pool_destroy(to_sync->pool);
    }

  return APR_SUCCESS;
}

svn_error_t *
svn_fs_x__batch_fsync_create(svn_fs_x__batch_fsync_t **result_p,
                             svn_boolean_t flush_to_disk,
                             apr_pool_t *result_pool)
{
  svn_fs_x__batch_fsync_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->files = svn_hash__make(result_pool);
  result->flush_to_disk = flush_to_disk;

  SVN_ERR(waitable_counter__create(&result->counter, result_pool));
  apr_pool_cleanup_register(result_pool, result, fsync_batch_cleanup,
                            apr_pool_cleanup_null);

  *result_p = result;

  return SVN_NO_ERROR;
}

/* If BATCH does not contain a handle for PATH, yet, create one with FLAGS
 * and add it to BATCH.  Set *FILE to the open file handle.
 * Use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
internal_open_file(apr_file_t **file,
                   svn_fs_x__batch_fsync_t *batch,
                   const char *path,
                   apr_int32_t flags,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  apr_pool_t *pool;
  to_sync_t *to_sync;
#ifdef SVN_ON_POSIX
  svn_boolean_t is_new_file;
#endif

  /* If we already have a handle for PATH, return that. */
  to_sync = svn_hash_gets(batch->files, path);
  if (to_sync)
    {
      *file = to_sync->file;
      return SVN_NO_ERROR;
    }

  /* Calling fsync in PATH is going to be expensive in any case, so we can
   * allow for some extra overhead figuring out whether the file already
   * exists.  If it doesn't, be sure to schedule parent folder updates, if
   * required on this platform.
   *
   * See svn_fs_x__batch_fsync_new_path() for when such extra fsyncs may be
   * needed at all. */

#ifdef SVN_ON_POSIX

  is_new_file = FALSE;
  if (flags & APR_CREATE)
    {
      svn_node_kind_t kind;
      /* We might actually be about to create a new file.
       * Check whether the file already exists. */
      SVN_ERR(svn_io_check_path(path, &kind, scratch_pool));
      is_new_file = kind == svn_node_none;
    }

#endif

  /* To be able to process each file in a separate thread, they must use
   * separate, thread-safe pools.  Allocating a sub-pool from the standard
   * memory pool achieves exactly that. */
  pool = svn_pool_create(NULL);
  err = svn_io_file_open(file, path, flags, APR_OS_DEFAULT, pool);
  if (err)
    {
      svn_pool_destroy(pool);
      return svn_error_trace(err);
    }

  to_sync = apr_pcalloc(pool, sizeof(*to_sync));
  to_sync->file = *file;
  to_sync->pool = pool;
  to_sync->result = SVN_NO_ERROR;
  to_sync->counter = batch->counter;

  svn_hash_sets(batch->files,
                apr_pstrdup(apr_hash_pool_get(batch->files), path),
                to_sync);

  /* If we just created a new file, schedule any additional necessary fsyncs.
   * Note that this can only recurse once since the parent folder already
   * exists on disk. */
#ifdef SVN_ON_POSIX

  if (is_new_file)
    SVN_ERR(svn_fs_x__batch_fsync_new_path(batch, path, scratch_pool));

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__batch_fsync_open_file(apr_file_t **file,
                                svn_fs_x__batch_fsync_t *batch,
                                const char *filename,
                                apr_pool_t *scratch_pool)
{
  apr_off_t offset = 0;

  SVN_ERR(internal_open_file(file, batch, filename, FILE_FLAGS,
                             scratch_pool));
  SVN_ERR(svn_io_file_seek(*file, APR_SET, &offset, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__batch_fsync_new_path(svn_fs_x__batch_fsync_t *batch,
                               const char *path,
                               apr_pool_t *scratch_pool)
{
  apr_file_t *file;

#ifdef SVN_ON_POSIX

  /* On POSIX, we need to sync the parent directory because it contains
   * the name for the file / folder given by PATH. */
  path = svn_dirent_dirname(path, scratch_pool);
  SVN_ERR(internal_open_file(&file, batch, path, APR_READ, scratch_pool));

#else

  svn_node_kind_t kind;

  /* On non-POSIX systems, we assume that sync'ing the given PATH is the
   * right thing to do.  Also, we assume that only files may be sync'ed. */
  SVN_ERR(svn_io_check_path(path, &kind, scratch_pool));
  if (kind == svn_node_file)
    SVN_ERR(internal_open_file(&file, batch, path, FILE_FLAGS,
                               scratch_pool));

#endif

  return SVN_NO_ERROR;
}

/* Thread-pool task Flush the to_sync_t instance given by DATA. */
static void * APR_THREAD_FUNC
flush_task(apr_thread_t *tid,
           void *data)
{
  to_sync_t *to_sync = data;

  to_sync->result = svn_error_trace(svn_io_file_flush_to_disk
                                        (to_sync->file, to_sync->pool));

  /* As soon as the increment call returns, TO_SYNC may be invalid
     (the main thread may have woken up and released the struct.

     Therefore, we cannot chain this error into TO_SYNC->RESULT.
     OTOH, the main thread will probably deadlock anyway if we got
     an error here, thus there is no point in trying to tell the
     main thread what the problem was. */
  svn_error_clear(waitable_counter__increment(to_sync->counter));

  return NULL;
}

svn_error_t *
svn_fs_x__batch_fsync_run(svn_fs_x__batch_fsync_t *batch,
                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* Number of tasks sent to the thread pool. */
  int tasks = 0;

  /* Because we allocated the open files from our global pool, don't bail
   * out on the first error.  Instead, process all files and but accumulate
   * the errors in this chain.
   */
  svn_error_t *chain = SVN_NO_ERROR;

  /* First, flush APR-internal buffers. This should minimize / prevent the
   * introduction of additional meta-data changes during the next phase.
   * We might otherwise issue redundant fsyncs.
   */
  for (hi = apr_hash_first(scratch_pool, batch->files);
       hi;
       hi = apr_hash_next(hi))
    {
      to_sync_t *to_sync = apr_hash_this_val(hi);
      to_sync->result = svn_error_trace(svn_io_file_flush
                                           (to_sync->file, to_sync->pool));
    }

  /* Make sure the task completion counter is set to 0. */
  chain = svn_error_compose_create(chain,
                                   waitable_counter__reset(batch->counter));

  /* Start the actual fsyncing process. */
  if (batch->flush_to_disk)
    {
      for (hi = apr_hash_first(scratch_pool, batch->files);
           hi;
           hi = apr_hash_next(hi))
        {
          to_sync_t *to_sync = apr_hash_this_val(hi);

#if APR_HAS_THREADS

          /* Forgot to call _init() or cleaned up the owning pool too early?
           */
          SVN_ERR_ASSERT(thread_pool);

          /* If there are multiple fsyncs to perform, run them in parallel.
           * Otherwise, skip the thread-pool and synchronization overhead. */
          if (apr_hash_count(batch->files) > 1)
            {
              apr_status_t status = APR_SUCCESS;
              status = apr_thread_pool_push(thread_pool, flush_task, to_sync,
                                            0, NULL);
              if (status)
                to_sync->result = svn_error_wrap_apr(status,
                                                     _("Can't push task"));
              else
                tasks++;
            }
          else

#endif

            {
              to_sync->result = svn_error_trace(svn_io_file_flush_to_disk
                                                  (to_sync->file,
                                                   to_sync->pool));
            }
        }
    }

  /* Wait for all outstanding flush operations to complete. */
  chain = svn_error_compose_create(chain,
                                   waitable_counter__wait_for(batch->counter,
                                                              tasks));

  /* Collect the results, close all files and release memory. */
  for (hi = apr_hash_first(scratch_pool, batch->files);
       hi;
       hi = apr_hash_next(hi))
    {
      to_sync_t *to_sync = apr_hash_this_val(hi);
      if (batch->flush_to_disk)
        chain = svn_error_compose_create(chain, to_sync->result);

      chain = svn_error_compose_create(chain,
                                       svn_io_file_close(to_sync->file,
                                                         scratch_pool));
      svn_pool_destroy(to_sync->pool);
    }

  /* Don't process any file / folder twice. */
  apr_hash_clear(batch->files);

  /* Report the errors that we encountered. */
  return svn_error_trace(chain);
}
