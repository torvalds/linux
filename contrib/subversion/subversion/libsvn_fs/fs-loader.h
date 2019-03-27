/*
 * fs_loader.h:  Declarations for the FS loader library
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


#ifndef LIBSVN_FS_LOADER_H
#define LIBSVN_FS_LOADER_H

#include "svn_types.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "private/svn_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The FS loader library implements the a front end to "filesystem
   abstract providers" (FSAPs), which implement the svn_fs API.

   The loader library divides up the FS API into several categories:

     - Top-level functions, which operate on paths to an FS
     - Functions which operate on an FS object
     - Functions which operate on a transaction object
     - Functions which operate on a root object
     - Functions which operate on a history object
     - Functions which operate on a noderev-ID object

   Some generic fields of the FS, transaction, root, and history
   objects are defined by the loader library; the rest are stored in
   the "fsap_data" field which is defined by the FSAP.  Likewise, some
   of the very simple svn_fs API functions (such as svn_fs_root_fs)
   are defined by the loader library, while the rest are implemented
   through vtable calls defined by the FSAP.

   If you are considering writing a new database-backed filesystem
   implementation, it may be appropriate to add a second, lower-level
   abstraction to the libsvn_fs_base library which currently
   implements the BDB filesystem type.  Consult the dev list for
   details on the "FSP-level" abstraction concept.
*/



/*** Top-level library vtable type ***/

typedef struct fs_library_vtable_t
{
  /* This field should always remain first in the vtable.
     Apart from that, it can be changed however you like, since exact
     version equality is required between loader and module.  This policy
     was weaker during 1.1.x, but only in ways which do not conflict with
     this statement, now that the minor version has increased. */
  const svn_version_t *(*get_version)(void);

  /* The open_fs/create/open_fs_for_recovery/upgrade_fs functions must
     use the common_pool_lock to serialize the access to the common_pool
     parameter for allocating fs-global objects such as an env cache. */
  svn_error_t *(*create)(svn_fs_t *fs, const char *path,
                         svn_mutex__t *common_pool_lock,
                         apr_pool_t *scratch_pool,
                         apr_pool_t *common_pool);
  svn_error_t *(*open_fs)(svn_fs_t *fs, const char *path,
                          svn_mutex__t *common_pool_lock,
                          apr_pool_t *scratch_pool,
                          apr_pool_t *common_pool);
  /* open_for_recovery() is like open(), but used to fill in an fs pointer
     that will be passed to recover().  We assume that the open() method
     might not be immediately appropriate for recovery. */
  svn_error_t *(*open_fs_for_recovery)(svn_fs_t *fs, const char *path,
                                       svn_mutex__t *common_pool_lock,
                                       apr_pool_t *pool,
                                       apr_pool_t *common_pool);
  svn_error_t *(*upgrade_fs)(svn_fs_t *fs,
                             const char *path,
                             svn_fs_upgrade_notify_t notify_func,
                             void *notify_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             svn_mutex__t *common_pool_lock,
                             apr_pool_t *scratch_pool,
                             apr_pool_t *common_pool);
  svn_error_t *(*verify_fs)(svn_fs_t *fs, const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_fs_progress_notify_func_t notify_func,
                            void *notify_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_mutex__t *common_pool_lock,
                            apr_pool_t *pool,
                            apr_pool_t *common_pool);
  svn_error_t *(*delete_fs)(const char *path, apr_pool_t *pool);
  svn_error_t *(*hotcopy)(svn_fs_t *src_fs,
                          svn_fs_t *dst_fs,
                          const char *src_path,
                          const char *dst_path,
                          svn_boolean_t clean,
                          svn_boolean_t incremental,
                          svn_fs_hotcopy_notify_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_mutex__t *common_pool_lock,
                          apr_pool_t *pool,
                          apr_pool_t *common_pool);
  const char *(*get_description)(void);
  svn_error_t *(*recover)(svn_fs_t *fs,
                          svn_cancel_func_t cancel_func, void *cancel_baton,
                          apr_pool_t *pool);
  svn_error_t *(*pack_fs)(svn_fs_t *fs, const char *path,
                          svn_fs_pack_notify_t notify_func, void *notify_baton,
                          svn_cancel_func_t cancel_func, void *cancel_baton,
                          svn_mutex__t *common_pool_lock,
                          apr_pool_t *pool, apr_pool_t *common_pool);

  /* Provider-specific functions should go here, even if they could go
     in an object vtable, so that they are all kept together. */
  svn_error_t *(*bdb_logfiles)(apr_array_header_t **logfiles,
                               const char *path, svn_boolean_t only_unused,
                               apr_pool_t *pool);

  /* This is to let the base provider implement the deprecated
     svn_fs_parse_id, which we've decided doesn't belong in the FS
     API.  If we change our minds and decide to add a real
     svn_fs_parse_id variant which takes an FS object, it should go
     into the FS vtable. */
  svn_fs_id_t *(*parse_id)(const char *data, apr_size_t len,
                           apr_pool_t *pool);
  /* Allow an FSAP to call svn_fs_open(), which is in a higher-level library
     (libsvn_fs-1.so) and cannot easily be moved to libsvn_fs_util. */
  svn_error_t *(*set_svn_fs_open)(svn_fs_t *fs,
                                  svn_error_t *(*svn_fs_open_)(svn_fs_t **,
                                                               const char *,
                                                               apr_hash_t *,
                                                               apr_pool_t *,
                                                               apr_pool_t *));
  /* For svn_fs_info_fsfs_dup(). */
  void *(*info_fsap_dup)(const void *fsap_info,
                         apr_pool_t *result_pool);
} fs_library_vtable_t;

/* This is the type of symbol an FS module defines to fetch the
   library vtable. The LOADER_VERSION parameter must remain first in
   the list, and the function must use the C calling convention on all
   platforms, so that the init functions can safely read the version
   parameter.  The COMMON_POOL parameter must be a pool with a greater
   lifetime than the fs module so that fs global state can be kept
   in it and cleaned up on termination before the fs module is unloaded.
   Calls to these functions are globally serialized so that they have
   exclusive access to the COMMON_POOL parameter.

   ### need to force this to be __cdecl on Windows... how?? */
typedef svn_error_t *(*fs_init_func_t)(const svn_version_t *loader_version,
                                       fs_library_vtable_t **vtable,
                                       apr_pool_t* common_pool);

/* Here are the declarations for the FS module init functions.  If we
   are using DSO loading, they won't actually be linked into
   libsvn_fs.  Note that these private functions have a common_pool
   parameter that may be used for fs module scoped variables such as
   the bdb cache.  This will be the same common_pool that is passed
   to the create and open functions and these init functions (as well
   as the open and create functions) are globally serialized so that
   they have exclusive access to the common_pool. */
#include "../libsvn_fs_base/fs_init.h"
#include "../libsvn_fs_fs/fs_init.h"
#include "../libsvn_fs_x/fs_init.h"



/*** vtable types for the abstract FS objects ***/

typedef struct fs_vtable_t
{
  svn_error_t *(*youngest_rev)(svn_revnum_t *youngest_p, svn_fs_t *fs,
                               apr_pool_t *pool);
  svn_error_t *(*refresh_revprops)(svn_fs_t *fs, apr_pool_t *scratch_pool);
  svn_error_t *(*revision_prop)(svn_string_t **value_p, svn_fs_t *fs,
                                svn_revnum_t rev, const char *propname,
                                svn_boolean_t refresh,
                                apr_pool_t *result_pool, 
                                apr_pool_t *scratch_pool);
  svn_error_t *(*revision_proplist)(apr_hash_t **table_p, svn_fs_t *fs,
                                    svn_revnum_t rev,
                                    svn_boolean_t refresh,
                                    apr_pool_t *result_pool, 
                                    apr_pool_t *scratch_pool);
  svn_error_t *(*change_rev_prop)(svn_fs_t *fs, svn_revnum_t rev,
                                  const char *name,
                                  const svn_string_t *const *old_value_p,
                                  const svn_string_t *value,
                                  apr_pool_t *pool);
  /* There is no get_uuid(); see svn_fs_t.uuid docstring. */
  svn_error_t *(*set_uuid)(svn_fs_t *fs, const char *uuid, apr_pool_t *pool);
  svn_error_t *(*revision_root)(svn_fs_root_t **root_p, svn_fs_t *fs,
                                svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*begin_txn)(svn_fs_txn_t **txn_p, svn_fs_t *fs,
                            svn_revnum_t rev, apr_uint32_t flags,
                            apr_pool_t *pool);
  svn_error_t *(*open_txn)(svn_fs_txn_t **txn, svn_fs_t *fs,
                           const char *name, apr_pool_t *pool);
  svn_error_t *(*purge_txn)(svn_fs_t *fs, const char *txn_id,
                            apr_pool_t *pool);
  svn_error_t *(*list_transactions)(apr_array_header_t **names_p,
                                    svn_fs_t *fs, apr_pool_t *pool);
  svn_error_t *(*deltify)(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*lock)(svn_fs_t *fs,
                       apr_hash_t *targets,
                       const char *comment, svn_boolean_t is_dav_comment,
                       apr_time_t expiration_date, svn_boolean_t steal_lock,
                       svn_fs_lock_callback_t lock_callback, void *lock_baton,
                       apr_pool_t *result_pool, apr_pool_t *scratch_pool);
  svn_error_t *(*generate_lock_token)(const char **token, svn_fs_t *fs,
                                      apr_pool_t *pool);
  svn_error_t *(*unlock)(svn_fs_t *fs, apr_hash_t *targets,
                         svn_boolean_t break_lock,
                         svn_fs_lock_callback_t lock_callback, void *lock_baton,
                         apr_pool_t *result_pool, apr_pool_t *scratch_pool);
  svn_error_t *(*get_lock)(svn_lock_t **lock, svn_fs_t *fs,
                           const char *path, apr_pool_t *pool);
  svn_error_t *(*get_locks)(svn_fs_t *fs, const char *path, svn_depth_t depth,
                            svn_fs_get_locks_callback_t get_locks_func,
                            void *get_locks_baton,
                            apr_pool_t *pool);
  svn_error_t *(*info_format)(int *fs_format,
                              svn_version_t **supports_version,
                              svn_fs_t *fs,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);
  svn_error_t *(*info_config_files)(apr_array_header_t **files,
                                    svn_fs_t *fs,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);
  svn_error_t *(*info_fsap)(const void **fsap_info,
                            svn_fs_t *fs,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);
  /* info_fsap_dup is in the library vtable. */
  svn_error_t *(*verify_root)(svn_fs_root_t *root,
                              apr_pool_t *pool);
  svn_error_t *(*freeze)(svn_fs_t *fs,
                         svn_fs_freeze_func_t freeze_func,
                         void *freeze_baton, apr_pool_t *pool);
  svn_error_t *(*bdb_set_errcall)(svn_fs_t *fs,
                                  void (*handler)(const char *errpfx,
                                                  char *msg));
} fs_vtable_t;


typedef struct txn_vtable_t
{
  svn_error_t *(*commit)(const char **conflict_p, svn_revnum_t *new_rev,
                         svn_fs_txn_t *txn, apr_pool_t *pool);
  svn_error_t *(*abort)(svn_fs_txn_t *txn, apr_pool_t *pool);
  svn_error_t *(*get_prop)(svn_string_t **value_p, svn_fs_txn_t *txn,
                           const char *propname, apr_pool_t *pool);
  svn_error_t *(*get_proplist)(apr_hash_t **table_p, svn_fs_txn_t *txn,
                               apr_pool_t *pool);
  svn_error_t *(*change_prop)(svn_fs_txn_t *txn, const char *name,
                              const svn_string_t *value, apr_pool_t *pool);
  svn_error_t *(*root)(svn_fs_root_t **root_p, svn_fs_txn_t *txn,
                       apr_pool_t *pool);
  svn_error_t *(*change_props)(svn_fs_txn_t *txn, const apr_array_header_t *props,
                               apr_pool_t *pool);
} txn_vtable_t;


/* Some of these operations accept multiple root arguments.  Since the
   roots may not all have the same vtable, we need a rule to determine
   which root's vtable is used.  The rule is: if one of the roots is
   named "target", we use that root's vtable; otherwise, we use the
   first root argument's vtable.
   These callbacks correspond to svn_fs_* functions in include/svn_fs.h,
   see there for details.
   Note: delete_node() corresponds to svn_fs_delete(). */
typedef struct root_vtable_t
{
  /* Determining what has changed in a root */
  svn_error_t *(*paths_changed)(apr_hash_t **changed_paths_p,
                                svn_fs_root_t *root,
                                apr_pool_t *pool);
  svn_error_t *(*report_changes)(svn_fs_path_change_iterator_t **iterator,
                                 svn_fs_root_t *root,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

  /* Generic node operations */
  svn_error_t *(*check_path)(svn_node_kind_t *kind_p, svn_fs_root_t *root,
                             const char *path, apr_pool_t *pool);
  svn_error_t *(*node_history)(svn_fs_history_t **history_p,
                               svn_fs_root_t *root, const char *path,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);
  svn_error_t *(*node_id)(const svn_fs_id_t **id_p, svn_fs_root_t *root,
                          const char *path, apr_pool_t *pool);
  svn_error_t *(*node_relation)(svn_fs_node_relation_t *relation,
                                svn_fs_root_t *root_a, const char *path_a,
                                svn_fs_root_t *root_b, const char *path_b,
                                apr_pool_t *scratch_pool);
  svn_error_t *(*node_created_rev)(svn_revnum_t *revision,
                                   svn_fs_root_t *root, const char *path,
                                   apr_pool_t *pool);
  svn_error_t *(*node_origin_rev)(svn_revnum_t *revision,
                                  svn_fs_root_t *root, const char *path,
                                  apr_pool_t *pool);
  svn_error_t *(*node_created_path)(const char **created_path,
                                    svn_fs_root_t *root, const char *path,
                                    apr_pool_t *pool);
  svn_error_t *(*delete_node)(svn_fs_root_t *root, const char *path,
                              apr_pool_t *pool);
  svn_error_t *(*copy)(svn_fs_root_t *from_root, const char *from_path,
                       svn_fs_root_t *to_root, const char *to_path,
                       apr_pool_t *pool);
  svn_error_t *(*revision_link)(svn_fs_root_t *from_root,
                                svn_fs_root_t *to_root,
                                const char *path,
                                apr_pool_t *pool);
  svn_error_t *(*copied_from)(svn_revnum_t *rev_p, const char **path_p,
                              svn_fs_root_t *root, const char *path,
                              apr_pool_t *pool);
  svn_error_t *(*closest_copy)(svn_fs_root_t **root_p, const char **path_p,
                               svn_fs_root_t *root, const char *path,
                               apr_pool_t *pool);

  /* Property operations */
  svn_error_t *(*node_prop)(svn_string_t **value_p, svn_fs_root_t *root,
                            const char *path, const char *propname,
                            apr_pool_t *pool);
  svn_error_t *(*node_proplist)(apr_hash_t **table_p, svn_fs_root_t *root,
                                const char *path, apr_pool_t *pool);
  svn_error_t *(*node_has_props)(svn_boolean_t *has_props, svn_fs_root_t *root,
                                 const char *path, apr_pool_t *scratch_pool);
  svn_error_t *(*change_node_prop)(svn_fs_root_t *root, const char *path,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);
  svn_error_t *(*props_changed)(int *changed_p, svn_fs_root_t *root1,
                                const char *path1, svn_fs_root_t *root2,
                                const char *path2, svn_boolean_t strict,
                                apr_pool_t *scratch_pool);

  /* Directories */
  svn_error_t *(*dir_entries)(apr_hash_t **entries_p, svn_fs_root_t *root,
                              const char *path, apr_pool_t *pool);
  svn_error_t *(*dir_optimal_order)(apr_array_header_t **ordered_p,
                                    svn_fs_root_t *root,
                                    apr_hash_t *entries,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);
  svn_error_t *(*make_dir)(svn_fs_root_t *root, const char *path,
                           apr_pool_t *pool);

  /* Files */
  svn_error_t *(*file_length)(svn_filesize_t *length_p, svn_fs_root_t *root,
                              const char *path, apr_pool_t *pool);
  svn_error_t *(*file_checksum)(svn_checksum_t **checksum,
                                svn_checksum_kind_t kind, svn_fs_root_t *root,
                                const char *path, apr_pool_t *pool);
  svn_error_t *(*file_contents)(svn_stream_t **contents,
                                svn_fs_root_t *root, const char *path,
                                apr_pool_t *pool);
  svn_error_t *(*try_process_file_contents)(svn_boolean_t *success,
                                            svn_fs_root_t *target_root,
                                            const char *target_path,
                                            svn_fs_process_contents_func_t processor,
                                            void* baton,
                                            apr_pool_t *pool);
  svn_error_t *(*make_file)(svn_fs_root_t *root, const char *path,
                            apr_pool_t *pool);
  svn_error_t *(*apply_textdelta)(svn_txdelta_window_handler_t *contents_p,
                                  void **contents_baton_p,
                                  svn_fs_root_t *root, const char *path,
                                  svn_checksum_t *base_checksum,
                                  svn_checksum_t *result_checksum,
                                  apr_pool_t *pool);
  svn_error_t *(*apply_text)(svn_stream_t **contents_p, svn_fs_root_t *root,
                             const char *path, svn_checksum_t *result_checksum,
                             apr_pool_t *pool);
  svn_error_t *(*contents_changed)(int *changed_p, svn_fs_root_t *root1,
                                   const char *path1, svn_fs_root_t *root2,
                                   const char *path2, svn_boolean_t strict,
                                   apr_pool_t *scratch_pool);
  svn_error_t *(*get_file_delta_stream)(svn_txdelta_stream_t **stream_p,
                                        svn_fs_root_t *source_root,
                                        const char *source_path,
                                        svn_fs_root_t *target_root,
                                        const char *target_path,
                                        apr_pool_t *pool);

  /* Merging. */
  svn_error_t *(*merge)(const char **conflict_p,
                        svn_fs_root_t *source_root,
                        const char *source_path,
                        svn_fs_root_t *target_root,
                        const char *target_path,
                        svn_fs_root_t *ancestor_root,
                        const char *ancestor_path,
                        apr_pool_t *pool);
  /* Mergeinfo. */
  svn_error_t *(*get_mergeinfo)(svn_fs_root_t *root,
                                const apr_array_header_t *paths,
                                svn_mergeinfo_inheritance_t inherit,
                                svn_boolean_t include_descendants,
                                svn_boolean_t adjust_inherited_mergeinfo,
                                svn_fs_mergeinfo_receiver_t receiver,
                                void *baton,
                                apr_pool_t *scratch_pool);
} root_vtable_t;


typedef struct changes_iterator_vtable_t
{
  svn_error_t *(*get)(svn_fs_path_change3_t **change,
                      svn_fs_path_change_iterator_t *iterator);
} changes_iterator_vtable_t;


typedef struct history_vtable_t
{
  svn_error_t *(*prev)(svn_fs_history_t **prev_history_p,
                       svn_fs_history_t *history, svn_boolean_t cross_copies,
                       apr_pool_t *result_pool, apr_pool_t *scratch_pool);
  svn_error_t *(*location)(const char **path, svn_revnum_t *revision,
                           svn_fs_history_t *history, apr_pool_t *pool);
} history_vtable_t;


typedef struct id_vtable_t
{
  svn_string_t *(*unparse)(const svn_fs_id_t *id,
                           apr_pool_t *pool);
  svn_fs_node_relation_t (*compare)(const svn_fs_id_t *a,
                                    const svn_fs_id_t *b);
} id_vtable_t;



/*** Definitions of the abstract FS object types ***/

/* These are transaction properties that correspond to the bitfields
   in the 'flags' argument to svn_fs_begin_txn2().  */
#define SVN_FS__PROP_TXN_CHECK_LOCKS           SVN_PROP_PREFIX "check-locks"
#define SVN_FS__PROP_TXN_CHECK_OOD             SVN_PROP_PREFIX "check-ood"
/* Set to "0" at the start of the txn, to "1" when svn:date changes. */
#define SVN_FS__PROP_TXN_CLIENT_DATE           SVN_PROP_PREFIX "client-date"

struct svn_fs_t
{
  /* The pool in which this fs object is allocated */
  apr_pool_t *pool;

  /* The path to the repository's top-level directory */
  char *path;

  /* A callback for printing warning messages */
  svn_fs_warning_callback_t warning;
  void *warning_baton;

  /* The filesystem configuration */
  apr_hash_t *config;

  /* An access context indicating who's using the fs */
  svn_fs_access_t *access_ctx;

  /* FSAP-specific vtable and private data */
  const fs_vtable_t *vtable;
  void *fsap_data;

  /* UUID, stored by open(), create(), and set_uuid(). */
  const char *uuid;
};


struct svn_fs_txn_t
{
  /* The filesystem to which this transaction belongs */
  svn_fs_t *fs;

  /* The revision on which this transaction is based, or
     SVN_INVALID_REVISION if the transaction is not based on a
     revision at all */
  svn_revnum_t base_rev;

  /* The ID of this transaction */
  const char *id;

  /* FSAP-specific vtable and private data */
  const txn_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_root_t
{
  /* A pool managing this root (and only this root!) */
  apr_pool_t *pool;

  /* The filesystem to which this root belongs */
  svn_fs_t *fs;

  /* The kind of root this is */
  svn_boolean_t is_txn_root;

  /* For transaction roots, the name of the transaction  */
  const char *txn;

  /* For transaction roots, flags describing the txn's behavior. */
  apr_uint32_t txn_flags;

  /* For revision roots, the number of the revision; for transaction
     roots, the number of the revision on which the transaction is
     based. */
  svn_revnum_t rev;

  /* FSAP-specific vtable and private data */
  const root_vtable_t *vtable;
  void *fsap_data;
};

struct svn_fs_path_change_iterator_t
{
  /* FSAP-specific vtable and private data */
  const changes_iterator_vtable_t *vtable;
  void *fsap_data;
};

struct svn_fs_history_t
{
  /* FSAP-specific vtable and private data */
  const history_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_id_t
{
  /* FSAP-specific vtable and private data */
  const id_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_access_t
{
  /* An authenticated username using the fs */
  const char *username;

  /* A collection of lock-tokens supplied by the fs caller.
     Hash maps (const char *) UUID --> path where path can be the
     magic value (void *) 1 if no path was specified.
     fs functions should really only be interested whether a UUID
     exists as a hash key at all;  the value is irrelevant. */
  apr_hash_t *lock_tokens;
};

struct svn_fs_lock_target_t
{
  const char *token;
  svn_revnum_t current_rev;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBSVN_FS_LOADER_H */
