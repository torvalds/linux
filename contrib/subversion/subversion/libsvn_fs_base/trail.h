/* trail.h : internal interface to backing out of aborted Berkeley DB txns
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

#ifndef SVN_LIBSVN_FS_TRAIL_H
#define SVN_LIBSVN_FS_TRAIL_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include <apr_pools.h>
#include "svn_fs.h"
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* "How do I get a trail object?  All these functions in the
   filesystem expect them, and I can't find a function that returns
   one."

   Well, there isn't a function that returns a trail.  All trails come
   from svn_fs_base__retry_txn.  Here's how to use that:

   When using Berkeley DB transactions to protect the integrity of a
   database, there are several things you need to keep in mind:

   - Any Berkeley DB operation you perform as part of a Berkeley DB
     transaction may return DB_LOCK_DEADLOCK, meaning that your
     operation interferes with some other transaction in progress.
     When this happens, you must abort the transaction, which undoes
     all the changes you've made so far, and try it again.  So every
     piece of code you ever write to bang on the DB needs to be
     wrapped up in a retry loop.

   - If, while you're doing your database operations, you also change
     some in-memory data structures, then you may want to revert those
     changes if the transaction deadlocks and needs to be retried.

   - If you get a `real' error (i.e., something other than
     DB_LOCK_DEADLOCK), you must abort your DB transaction, to release
     its locks and return the database to its previous state.
     Similarly, you may want to unroll some changes you've made to
     in-memory data structures.

   - Since a transaction insulates you from database changes made by
     other processes, it's often possible to cache information about
     database contents while the transaction lasts.  However, this
     cache may become stale once your transaction is over.  So you may
     need to clear your cache once the transaction completes, either
     successfully or unsuccessfully.

   The `svn_fs_base__retry_txn' function and its friends help you manage
   some of that, in one nice package.

   To use it, write your code in a function like this:

       static svn_error_t *
       txn_body_do_my_thing (void *baton,
                             trail_t *trail)
       {
         ...
         Do everything which needs to be protected by a Berkeley DB
         transaction here.  Use TRAIL->db_txn as your Berkeley DB
         transaction, and do your allocation in TRAIL->pool.  Pass
         TRAIL on through to any functions which require one.

         If a Berkeley DB operation returns DB_LOCK_DEADLOCK, just
         return that using the normal Subversion error mechanism
         (using DB_ERR, for example); don't write a retry loop.  If you
         encounter some other kind of error, return it in the normal
         fashion.
         ...
       }

   Now, call svn_fs_base__retry_txn, passing a pointer to your function as
   an argument:

       err = svn_fs_base__retry_txn (fs, txn_body_do_my_thing, baton, pool);

   This will simply invoke your function `txn_body_do_my_thing',
   passing BATON through unchanged, and providing a fresh TRAIL
   object, containing a pointer to the filesystem object, a Berkeley
   DB transaction and an APR pool -- a subpool of POOL -- you should
   use.

   If your function returns a Subversion error wrapping a Berkeley DB
   DB_LOCK_DEADLOCK error, `svn_fs_base__retry_txn' will abort the trail's
   Berkeley DB transaction for you (thus undoing any database changes
   you've made), free the trail's subpool (thus undoing any allocation
   you may have done), and try the whole thing again with a new trail,
   containing a new Berkeley DB transaction and pool.

   If your function returns any other kind of Subversion error,
   `svn_fs_base__retry_txn' will abort the trail's Berkeley DB transaction,
   free the subpool, and return your error to its caller.

   If, heavens forbid, your function actually succeeds, returning
   SVN_NO_ERROR, `svn_fs_base__retry_txn' commits the trail's Berkeley DB
   transaction, thus making your DB changes permanent, leaves the
   trail's pool alone so all the objects it contains are still
   around (unless you request otherwise), and returns SVN_NO_ERROR.


   Keep the amount of work done in a trail small. C-Mike Pilato said to me:

   I want to draw your attention to something that you may or may not realize
   about designing for the BDB backend.  The 'trail' objects are (generally)
   representative of Berkeley DB transactions -- that part I'm sure you know.
   But you might not realize the value of keeping transactions as small as
   possible.  Berkeley DB will accumulate locks (which I believe are
   page-level, not as tight as row-level like you might hope) over the course
   of a transaction, releasing those locks only at transaction commit/abort.
   Berkeley DB backends are configured to have a maximum number of locks and
   lockers allowed, and it's easier than you might think to hit the max-locks
   thresholds (especially under high concurrency) and see an error (typically a
   "Cannot allocate memory") result from that.

   For example, in [a loop] you are writing a bunch of rows to the
   `changes' table.  Could be 10.  Could be 100,000.  100,000 writes and
   associated locks might be a problem or it might not.  But I use it as a way
   to encourage you to think about reducing the amount of work you spend in any
   one trail [...].
*/

struct trail_t
{
  /* A Berkeley DB transaction.  */
  DB_TXN *db_txn;

  /* The filesystem object with which this trail is associated. */
  svn_fs_t *fs;

  /* A pool to allocate things in as part of that transaction --- a
     subpool of the one passed to `begin_trail'.  We destroy this pool
     if we abort the transaction, and leave it around otherwise.  */
  apr_pool_t *pool;

#if defined(SVN_FS__TRAIL_DEBUG)
  struct trail_debug_t *trail_debug;
#endif
};
typedef struct trail_t trail_t;


/* Try a Berkeley DB transaction repeatedly until it doesn't deadlock.

   That is:
   - Begin a new Berkeley DB transaction, DB_TXN, in the filesystem FS.
   - Allocate a subpool of POOL, TXN_POOL.
   - Start a new trail, TRAIL, pointing to DB_TXN and TXN_POOL.
   - Apply TXN_BODY to BATON and TRAIL.  TXN_BODY should try to do
     some series of DB operations which needs to be atomic, using
     TRAIL->db_txn as the transaction, and TRAIL->pool for allocation.
     If a DB operation deadlocks, or if any other kind of error
     happens, TXN_BODY should simply return with an appropriate
     svn_error_t, E.
   - If TXN_BODY returns SVN_NO_ERROR, then commit the transaction,
     run any completion functions, and return SVN_NO_ERROR.  Do *not*
     free TXN_POOL (unless DESTROY_TRAIL_POOL is set).
   - If E is a Berkeley DB error indicating that a deadlock occurred,
     abort the DB transaction and free TXN_POOL.  Then retry the whole
     thing from the top.
   - If E is any other kind of error, free TXN_POOL and return E.

   One benefit of using this function is that it makes it easy to
   ensure that whatever transactions a filesystem function starts, it
   either aborts or commits before it returns.  If we don't somehow
   complete all our transactions, later operations could deadlock.  */
svn_error_t *
svn_fs_base__retry_txn(svn_fs_t *fs,
                       svn_error_t *(*txn_body)(void *baton,
                                                trail_t *trail),
                       void *baton,
                       svn_boolean_t destroy_trail_pool,
                       apr_pool_t *pool);

svn_error_t *
svn_fs_base__retry_debug(svn_fs_t *fs,
                         svn_error_t *(*txn_body)(void *baton,
                                                  trail_t *trail),
                         void *baton,
                         svn_boolean_t destroy_trail_pool,
                         apr_pool_t *pool,
                         const char *txn_body_fn_name,
                         const char *filename,
                         int line);

#if defined(SVN_FS__TRAIL_DEBUG)
#define svn_fs_base__retry_txn(fs, txn_body, baton, destroy, pool) \
  svn_fs_base__retry_debug(fs, txn_body, baton, destroy, pool,     \
                           #txn_body, __FILE__, __LINE__)
#endif


/* Try an action repeatedly until it doesn't deadlock.  This is
   exactly like svn_fs_base__retry_txn() (whose documentation you really
   should read) except that no Berkeley DB transaction is created. */
svn_error_t *svn_fs_base__retry(svn_fs_t *fs,
                                svn_error_t *(*txn_body)(void *baton,
                                                         trail_t *trail),
                                void *baton,
                                svn_boolean_t destroy_trail_pool,
                                apr_pool_t *pool);


/* Record that OPeration is being done on TABLE in the TRAIL. */
#if defined(SVN_FS__TRAIL_DEBUG)
void svn_fs_base__trail_debug(trail_t *trail, const char *table,
                              const char *op);
#else
#define svn_fs_base__trail_debug(trail, table, operation)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_TRAIL_H */
