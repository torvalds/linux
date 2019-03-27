/* revs-txns.c : operations on revision and transactions
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

#include <string.h>

#include <apr_tables.h>
#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_time.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_io.h"

#include "fs.h"
#include "dag.h"
#include "err.h"
#include "trail.h"
#include "tree.h"
#include "revs-txns.h"
#include "key-gen.h"
#include "id.h"
#include "bdb/rev-table.h"
#include "bdb/txn-table.h"
#include "bdb/copies-table.h"
#include "bdb/changes-table.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "private/svn_fs_util.h"


/*** Helpers ***/

/* Set *txn_p to a transaction object allocated in POOL for the
   transaction in FS whose id is TXN_ID.  If EXPECT_DEAD is set, this
   transaction must be a dead one, else an error is returned.  If
   EXPECT_DEAD is not set, the transaction must *not* be a dead one,
   else an error is returned. */
static svn_error_t *
get_txn(transaction_t **txn_p,
        svn_fs_t *fs,
        const char *txn_id,
        svn_boolean_t expect_dead,
        trail_t *trail,
        apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(svn_fs_bdb__get_txn(&txn, fs, txn_id, trail, pool));
  if (expect_dead && (txn->kind != transaction_kind_dead))
    return svn_error_createf(SVN_ERR_FS_TRANSACTION_NOT_DEAD, 0,
                             _("Transaction is not dead: '%s'"), txn_id);
  if ((! expect_dead) && (txn->kind == transaction_kind_dead))
    return svn_error_createf(SVN_ERR_FS_TRANSACTION_DEAD, 0,
                             _("Transaction is dead: '%s'"), txn_id);
  *txn_p = txn;
  return SVN_NO_ERROR;
}


/* This is only for symmetry with the get_txn() helper. */
#define put_txn svn_fs_bdb__put_txn



/*** Revisions ***/

/* Return the committed transaction record *TXN_P and its ID *TXN_ID
   (as long as those parameters aren't NULL) for the revision REV in
   FS as part of TRAIL.  */
static svn_error_t *
get_rev_txn(transaction_t **txn_p,
            const char **txn_id,
            svn_fs_t *fs,
            svn_revnum_t rev,
            trail_t *trail,
            apr_pool_t *pool)
{
  revision_t *revision;
  transaction_t *txn;

  SVN_ERR(svn_fs_bdb__get_rev(&revision, fs, rev, trail, pool));
  if (revision->txn_id == NULL)
    return svn_fs_base__err_corrupt_fs_revision(fs, rev);

  SVN_ERR(get_txn(&txn, fs, revision->txn_id, FALSE, trail, pool));
  if (txn->revision != rev)
    return svn_fs_base__err_corrupt_txn(fs, revision->txn_id);

  if (txn_p)
    *txn_p = txn;
  if (txn_id)
    *txn_id = revision->txn_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__rev_get_root(const svn_fs_id_t **root_id_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR(get_rev_txn(&txn, NULL, fs, rev, trail, pool));
  if (txn->root_id == NULL)
    return svn_fs_base__err_corrupt_fs_revision(fs, rev);

  *root_id_p = txn->root_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__rev_get_txn_id(const char **txn_id_p,
                            svn_fs_t *fs,
                            svn_revnum_t rev,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  revision_t *revision;

  SVN_ERR(svn_fs_bdb__get_rev(&revision, fs, rev, trail, pool));
  if (revision->txn_id == NULL)
    return svn_fs_base__err_corrupt_fs_revision(fs, rev);

  *txn_id_p = revision->txn_id;
  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_youngest_rev(void *baton, trail_t *trail)
{
  return svn_fs_bdb__youngest_rev(baton, trail->fs, trail, trail->pool);
}


svn_error_t *
svn_fs_base__youngest_rev(svn_revnum_t *youngest_p,
                          svn_fs_t *fs,
                          apr_pool_t *pool)
{
  svn_revnum_t youngest;
  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_youngest_rev, &youngest,
                                 TRUE, pool));
  *youngest_p = youngest;
  return SVN_NO_ERROR;
}


struct revision_proplist_args {
  apr_hash_t **table_p;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_revision_proplist(void *baton, trail_t *trail)
{
  struct revision_proplist_args *args = baton;
  transaction_t *txn;

  SVN_ERR(get_rev_txn(&txn, NULL, trail->fs, args->rev, trail, trail->pool));
  *(args->table_p) = txn->proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__revision_proplist(apr_hash_t **table_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               svn_boolean_t refresh,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  struct revision_proplist_args args;
  apr_hash_t *table;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.table_p = &table;
  args.rev = rev;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_revision_proplist, &args,
                                 FALSE, result_pool));

  *table_p = table ? table : apr_hash_make(result_pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__revision_prop(svn_string_t **value_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *propname,
                           svn_boolean_t refresh,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct revision_proplist_args args;
  apr_hash_t *table;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* Get the proplist. */
  args.table_p = &table;
  args.rev = rev;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_revision_proplist, &args,
                                 FALSE, result_pool));

  /* And then the prop from that list (if there was a list). */
  *value_p = svn_hash_gets(table, propname);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__set_rev_prop(svn_fs_t *fs,
                          svn_revnum_t rev,
                          const char *name,
                          const svn_string_t *const *old_value_p,
                          const svn_string_t *value,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;
  const char *txn_id;
  const svn_string_t *present_value;

  SVN_ERR(get_rev_txn(&txn, &txn_id, fs, rev, trail, pool));
  present_value = svn_hash_gets(txn->proplist, name);

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make(pool);

  /* Set the property. */
  if (old_value_p)
    {
      const svn_string_t *wanted_value = *old_value_p;
      if ((!wanted_value != !present_value)
          || (wanted_value && present_value
              && !svn_string_compare(wanted_value, present_value)))
        {
          /* What we expected isn't what we found. */
          return svn_error_createf(SVN_ERR_FS_PROP_BASEVALUE_MISMATCH, NULL,
                                   _("revprop '%s' has unexpected value in "
                                     "filesystem"),
                                   name);
        }
      /* Fall through. */
    }

  /* If the prop-set is a no-op, skip the actual write. */
  if ((!present_value && !value)
      || (present_value && value
          && svn_string_compare(present_value, value)))
    return SVN_NO_ERROR;

  svn_hash_sets(txn->proplist, name, value);

  /* Overwrite the revision. */
  return put_txn(fs, txn, txn_id, trail, pool);
}


struct change_rev_prop_args {
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *const *old_value_p;
  const svn_string_t *value;
};


static svn_error_t *
txn_body_change_rev_prop(void *baton, trail_t *trail)
{
  struct change_rev_prop_args *args = baton;

  return svn_fs_base__set_rev_prop(trail->fs, args->rev,
                                   args->name, args->old_value_p, args->value,
                                   trail, trail->pool);
}


svn_error_t *
svn_fs_base__change_rev_prop(svn_fs_t *fs,
                             svn_revnum_t rev,
                             const char *name,
                             const svn_string_t *const *old_value_p,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  struct change_rev_prop_args args;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.rev = rev;
  args.name = name;
  args.old_value_p = old_value_p;
  args.value = value;
  return svn_fs_base__retry_txn(fs, txn_body_change_rev_prop, &args,
                                TRUE, pool);
}



/*** Transactions ***/

svn_error_t *
svn_fs_base__txn_make_committed(svn_fs_t *fs,
                                const char *txn_name,
                                svn_revnum_t revision,
                                trail_t *trail,
                                apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));

  /* Make sure the TXN is not committed already. */
  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  /* Convert TXN to a committed transaction. */
  txn->base_id = NULL;
  txn->revision = revision;
  txn->kind = transaction_kind_committed;
  return put_txn(fs, txn, txn_name, trail, pool);
}


svn_error_t *
svn_fs_base__txn_get_revision(svn_revnum_t *revision,
                              svn_fs_t *fs,
                              const char *txn_name,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  *revision = txn->revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__get_txn_ids(const svn_fs_id_t **root_id_p,
                         const svn_fs_id_t **base_root_id_p,
                         svn_fs_t *fs,
                         const char *txn_name,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__set_txn_root(svn_fs_t *fs,
                          const char *txn_name,
                          const svn_fs_id_t *new_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  if (! svn_fs_base__id_eq(txn->root_id, new_id))
    {
      txn->root_id = new_id;
      SVN_ERR(put_txn(fs, txn, txn_name, trail, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__set_txn_base(svn_fs_t *fs,
                          const char *txn_name,
                          const svn_fs_id_t *new_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  if (! svn_fs_base__id_eq(txn->base_id, new_id))
    {
      txn->base_id = new_id;
      SVN_ERR(put_txn(fs, txn, txn_name, trail, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__add_txn_copy(svn_fs_t *fs,
                          const char *txn_name,
                          const char *copy_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;

  /* Get the transaction and ensure its mutability. */
  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  /* Allocate a new array if this transaction has no copies. */
  if (! txn->copies)
    txn->copies = apr_array_make(pool, 1, sizeof(copy_id));

  /* Add COPY_ID to the array. */
  APR_ARRAY_PUSH(txn->copies, const char *) = copy_id;

  /* Finally, write out the transaction. */
  return put_txn(fs, txn, txn_name, trail, pool);
}



/* Generic transaction operations.  */

struct txn_proplist_args {
  apr_hash_t **table_p;
  const char *id;
};


static svn_error_t *
txn_body_txn_proplist(void *baton, trail_t *trail)
{
  transaction_t *txn;
  struct txn_proplist_args *args = baton;

  SVN_ERR(get_txn(&txn, trail->fs, args->id, FALSE, trail, trail->pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(trail->fs, args->id);

  *(args->table_p) = txn->proplist;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__txn_proplist_in_trail(apr_hash_t **table_p,
                                   const char *txn_id,
                                   trail_t *trail)
{
  struct txn_proplist_args args;
  apr_hash_t *table;

  args.table_p = &table;
  args.id = txn_id;
  SVN_ERR(txn_body_txn_proplist(&args, trail));

  *table_p = table ? table : apr_hash_make(trail->pool);
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__txn_proplist(apr_hash_t **table_p,
                          svn_fs_txn_t *txn,
                          apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.table_p = &table;
  args.id = txn->id;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_txn_proplist, &args,
                                 FALSE, pool));

  *table_p = table ? table : apr_hash_make(pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__txn_prop(svn_string_t **value_p,
                      svn_fs_txn_t *txn,
                      const char *propname,
                      apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* Get the proplist. */
  args.table_p = &table;
  args.id = txn->id;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_txn_proplist, &args,
                                 FALSE, pool));

  /* And then the prop from that list (if there was a list). */
  *value_p = svn_hash_gets(table, propname);

  return SVN_NO_ERROR;
}



struct change_txn_prop_args {
  svn_fs_t *fs;
  const char *id;
  const char *name;
  const svn_string_t *value;
};


svn_error_t *
svn_fs_base__set_txn_prop(svn_fs_t *fs,
                          const char *txn_name,
                          const char *name,
                          const svn_string_t *value,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  transaction_t *txn;

  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, trail, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(fs, txn_name);

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make(pool);

  /* Set the property. */
  if (svn_hash_gets(txn->proplist, SVN_FS__PROP_TXN_CLIENT_DATE)
      && !strcmp(name, SVN_PROP_REVISION_DATE))
    svn_hash_sets(txn->proplist, SVN_FS__PROP_TXN_CLIENT_DATE,
                  svn_string_create("1", pool));
  svn_hash_sets(txn->proplist, name, value);

  /* Now overwrite the transaction. */
  return put_txn(fs, txn, txn_name, trail, pool);
}


static svn_error_t *
txn_body_change_txn_prop(void *baton, trail_t *trail)
{
  struct change_txn_prop_args *args = baton;
  return svn_fs_base__set_txn_prop(trail->fs, args->id, args->name,
                                   args->value, trail, trail->pool);
}


svn_error_t *
svn_fs_base__change_txn_prop(svn_fs_txn_t *txn,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  struct change_txn_prop_args args;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.id = txn->id;
  args.name = name;
  args.value = value;
  return svn_fs_base__retry_txn(fs, txn_body_change_txn_prop, &args,
                                TRUE, pool);
}


svn_error_t *
svn_fs_base__change_txn_props(svn_fs_txn_t *txn,
                              const apr_array_header_t *props,
                              apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  for (i = 0; i < props->nelts; i++)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_base__change_txn_prop(txn, prop->name,
                                           prop->value, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Creating a transaction */

static txn_vtable_t txn_vtable = {
  svn_fs_base__commit_txn,
  svn_fs_base__abort_txn,
  svn_fs_base__txn_prop,
  svn_fs_base__txn_proplist,
  svn_fs_base__change_txn_prop,
  svn_fs_base__txn_root,
  svn_fs_base__change_txn_props
};


/* Allocate and return a new transaction object in POOL for FS whose
   transaction ID is ID.  ID is not copied.  */
static svn_fs_txn_t *
make_txn(svn_fs_t *fs,
         const char *id,
         svn_revnum_t base_rev,
         apr_pool_t *pool)
{
  svn_fs_txn_t *txn = apr_pcalloc(pool, sizeof(*txn));

  txn->fs = fs;
  txn->id = id;
  txn->base_rev = base_rev;
  txn->vtable = &txn_vtable;
  txn->fsap_data = NULL;

  return txn;
}


struct begin_txn_args
{
  svn_fs_txn_t **txn_p;
  svn_revnum_t base_rev;
  apr_uint32_t flags;
};


static svn_error_t *
txn_body_begin_txn(void *baton, trail_t *trail)
{
  struct begin_txn_args *args = baton;
  const svn_fs_id_t *root_id;
  const char *txn_id;

  SVN_ERR(svn_fs_base__rev_get_root(&root_id, trail->fs, args->base_rev,
                                    trail, trail->pool));
  SVN_ERR(svn_fs_bdb__create_txn(&txn_id, trail->fs, root_id,
                                 trail, trail->pool));

  if (args->flags & SVN_FS_TXN_CHECK_OOD)
    {
      struct change_txn_prop_args cpargs;
      cpargs.fs = trail->fs;
      cpargs.id = txn_id;
      cpargs.name = SVN_FS__PROP_TXN_CHECK_OOD;
      cpargs.value = svn_string_create("true", trail->pool);

      SVN_ERR(txn_body_change_txn_prop(&cpargs, trail));
    }

  if (args->flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      struct change_txn_prop_args cpargs;
      cpargs.fs = trail->fs;
      cpargs.id = txn_id;
      cpargs.name = SVN_FS__PROP_TXN_CHECK_LOCKS;
      cpargs.value = svn_string_create("true", trail->pool);

      SVN_ERR(txn_body_change_txn_prop(&cpargs, trail));
    }

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.) Do
     this before setting CLIENT_DATE so that it is not recorded as an
     explicit setting. */
  {
    struct change_txn_prop_args cpargs;
    svn_string_t date;
    cpargs.fs = trail->fs;
    cpargs.id = txn_id;
    cpargs.name = SVN_PROP_REVISION_DATE;
    date.data  = svn_time_to_cstring(apr_time_now(), trail->pool);
    date.len = strlen(date.data);
    cpargs.value = &date;
    SVN_ERR(txn_body_change_txn_prop(&cpargs, trail));
  }

  if (args->flags & SVN_FS_TXN_CLIENT_DATE)
    {
      struct change_txn_prop_args cpargs;
      cpargs.fs = trail->fs;
      cpargs.id = txn_id;
      cpargs.name = SVN_FS__PROP_TXN_CLIENT_DATE;
      cpargs.value = svn_string_create("0", trail->pool);

      SVN_ERR(txn_body_change_txn_prop(&cpargs, trail));
    }

  *args->txn_p = make_txn(trail->fs, txn_id, args->base_rev, trail->pool);
  return SVN_NO_ERROR;
}

/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_base__begin_txn(svn_fs_txn_t **txn_p,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_uint32_t flags,
                       apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  struct begin_txn_args args;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.txn_p = &txn;
  args.base_rev = rev;
  args.flags = flags;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_begin_txn, &args, FALSE, pool));

  *txn_p = txn;

  return SVN_NO_ERROR;
}


struct open_txn_args
{
  svn_fs_txn_t **txn_p;
  const char *name;
};


static svn_error_t *
txn_body_open_txn(void *baton, trail_t *trail)
{
  struct open_txn_args *args = baton;
  transaction_t *fstxn;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  const char *txn_id;

  SVN_ERR(get_txn(&fstxn, trail->fs, args->name, FALSE, trail, trail->pool));
  if (fstxn->kind != transaction_kind_committed)
    {
      txn_id = svn_fs_base__id_txn_id(fstxn->base_id);
      SVN_ERR(svn_fs_base__txn_get_revision(&base_rev, trail->fs, txn_id,
                                            trail, trail->pool));
    }

  *args->txn_p = make_txn(trail->fs, args->name, base_rev, trail->pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__open_txn(svn_fs_txn_t **txn_p,
                      svn_fs_t *fs,
                      const char *name,
                      apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  struct open_txn_args args;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.txn_p = &txn;
  args.name = name;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_open_txn, &args, FALSE, pool));

  *txn_p = txn;
  return SVN_NO_ERROR;
}


struct cleanup_txn_args
{
  transaction_t **txn_p;
  const char *name;
};


static svn_error_t *
txn_body_cleanup_txn(void *baton, trail_t *trail)
{
  struct cleanup_txn_args *args = baton;
  return get_txn(args->txn_p, trail->fs, args->name, TRUE,
                 trail, trail->pool);
}


static svn_error_t *
txn_body_cleanup_txn_copy(void *baton, trail_t *trail)
{
  const char *copy_id = *(const char **)baton;
  svn_error_t *err = svn_fs_bdb__delete_copy(trail->fs, copy_id, trail,
                                             trail->pool);

  /* Copy doesn't exist?  No sweat. */
  if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_COPY))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}


static svn_error_t *
txn_body_cleanup_txn_changes(void *baton, trail_t *trail)
{
  const char *key = *(const char **)baton;

  return svn_fs_bdb__changes_delete(trail->fs, key, trail, trail->pool);
}


struct get_dirents_args
{
  apr_hash_t **dirents;
  const svn_fs_id_t *id;
  const char *txn_id;
};


static svn_error_t *
txn_body_get_dirents(void *baton, trail_t *trail)
{
  struct get_dirents_args *args = baton;
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_base__dag_get_node(&node, trail->fs, args->id,
                                    trail, trail->pool));

  /* If immutable, do nothing and return. */
  if (! svn_fs_base__dag_check_mutable(node, args->txn_id))
    return SVN_NO_ERROR;

  /* If a directory, do nothing and return. */
  *(args->dirents) = NULL;
  if (svn_fs_base__dag_node_kind(node) != svn_node_dir)
    return SVN_NO_ERROR;

  /* Else it's mutable.  Get its dirents. */
  return svn_fs_base__dag_dir_entries(args->dirents, node,
                                      trail, trail->pool);
}


struct remove_node_args
{
  const svn_fs_id_t *id;
  const char *txn_id;
};


static svn_error_t *
txn_body_remove_node(void *baton, trail_t *trail)
{
  struct remove_node_args *args = baton;
  return svn_fs_base__dag_remove_node(trail->fs, args->id, args->txn_id,
                                      trail, trail->pool);
}


static svn_error_t *
delete_txn_tree(svn_fs_t *fs,
                const svn_fs_id_t *id,
                const char *txn_id,
                apr_pool_t *pool)
{
  struct get_dirents_args dirent_args;
  struct remove_node_args rm_args;
  apr_hash_t *dirents = NULL;
  apr_hash_index_t *hi;
  svn_error_t *err;

  /* If this sucker isn't mutable, there's nothing to do. */
  if (strcmp(svn_fs_base__id_txn_id(id), txn_id) != 0)
    return SVN_NO_ERROR;

  /* See if the thing has dirents that need to be recursed upon.  If
     you can't find the thing itself, don't sweat it.  We probably
     already cleaned it up. */
  dirent_args.dirents = &dirents;
  dirent_args.id = id;
  dirent_args.txn_id = txn_id;
  err = svn_fs_base__retry_txn(fs, txn_body_get_dirents, &dirent_args,
                               FALSE, pool);
  if (err && (err->apr_err == SVN_ERR_FS_ID_NOT_FOUND))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* If there are dirents upon which to recurse ... recurse. */
  if (dirents)
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      /* Loop over hash entries */
      for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
        {
          void *val;
          svn_fs_dirent_t *dirent;

          svn_pool_clear(subpool);
          apr_hash_this(hi, NULL, NULL, &val);
          dirent = val;
          SVN_ERR(delete_txn_tree(fs, dirent->id, txn_id, subpool));
        }
      svn_pool_destroy(subpool);
    }

  /* Remove the node. */
  rm_args.id = id;
  rm_args.txn_id = txn_id;
  return svn_fs_base__retry_txn(fs, txn_body_remove_node, &rm_args,
                                TRUE, pool);
}


static svn_error_t *
txn_body_delete_txn(void *baton, trail_t *trail)
{
  const char *txn_id = *(const char **)baton;

  return svn_fs_bdb__delete_txn(trail->fs, txn_id, trail, trail->pool);
}


svn_error_t *
svn_fs_base__purge_txn(svn_fs_t *fs,
                       const char *txn_id,
                       apr_pool_t *pool)
{
  struct cleanup_txn_args args;
  transaction_t *txn;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* Open the transaction, expecting it to be dead. */
  args.txn_p = &txn;
  args.name = txn_id;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_cleanup_txn, &args,
                                 FALSE, pool));

  /* Delete the mutable portion of the tree hanging from the
     transaction (which should gracefully recover if we've already
     done this). */
  SVN_ERR(delete_txn_tree(fs, txn->root_id, txn_id, pool));

  /* Kill the transaction's changes (which should gracefully recover
     if...). */
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_cleanup_txn_changes,
                                 &txn_id, TRUE, pool));

  /* Kill the transaction's copies (which should gracefully...). */
  if (txn->copies)
    {
      int i;

      for (i = 0; i < txn->copies->nelts; i++)
        {
          SVN_ERR(svn_fs_base__retry_txn
                  (fs, txn_body_cleanup_txn_copy,
                   &APR_ARRAY_IDX(txn->copies, i, const char *),
                   TRUE, pool));
        }
    }

  /* Kill the transaction itself (which ... just kidding -- this has
     no graceful failure mode). */
  return svn_fs_base__retry_txn(fs, txn_body_delete_txn, &txn_id,
                                TRUE, pool);
}


static svn_error_t *
txn_body_abort_txn(void *baton, trail_t *trail)
{
  svn_fs_txn_t *txn = baton;
  transaction_t *fstxn;

  /* Get the transaction by its id, set it to "dead", and store the
     transaction. */
  SVN_ERR(get_txn(&fstxn, txn->fs, txn->id, FALSE, trail, trail->pool));
  if (fstxn->kind != transaction_kind_normal)
    return svn_fs_base__err_txn_not_mutable(txn->fs, txn->id);

  fstxn->kind = transaction_kind_dead;
  return put_txn(txn->fs, fstxn, txn->id, trail, trail->pool);
}


svn_error_t *
svn_fs_base__abort_txn(svn_fs_txn_t *txn,
                       apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(txn->fs, TRUE));

  /* Set the transaction to "dead". */
  SVN_ERR(svn_fs_base__retry_txn(txn->fs, txn_body_abort_txn, txn,
                                 TRUE, pool));

  /* Now, purge it. */
  SVN_ERR_W(svn_fs_base__purge_txn(txn->fs, txn->id, pool),
            _("Transaction aborted, but cleanup failed"));

  return SVN_NO_ERROR;
}


struct list_transactions_args
{
  apr_array_header_t **names_p;
  apr_pool_t *pool;
};

static svn_error_t *
txn_body_list_transactions(void* baton, trail_t *trail)
{
  struct list_transactions_args *args = baton;
  return svn_fs_bdb__get_txn_list(args->names_p, trail->fs,
                                  trail, args->pool);
}

svn_error_t *
svn_fs_base__list_transactions(apr_array_header_t **names_p,
                               svn_fs_t *fs,
                               apr_pool_t *pool)
{
  apr_array_header_t *names;
  struct list_transactions_args args;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  args.names_p = &names;
  args.pool = pool;
  SVN_ERR(svn_fs_base__retry(fs, txn_body_list_transactions, &args,
                             FALSE, pool));

  *names_p = names;
  return SVN_NO_ERROR;
}
