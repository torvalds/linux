/* fs.c --- creating, opening and closing filesystems
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
#include <stdio.h>
#include <string.h>

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "revs-txns.h"
#include "uuid.h"
#include "tree.h"
#include "id.h"
#include "lock.h"
#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "bdb/bdb-err.h"
#include "bdb/bdb_compat.h"
#include "bdb/env.h"
#include "bdb/nodes-table.h"
#include "bdb/rev-table.h"
#include "bdb/txn-table.h"
#include "bdb/copies-table.h"
#include "bdb/changes-table.h"
#include "bdb/reps-table.h"
#include "bdb/strings-table.h"
#include "bdb/uuids-table.h"
#include "bdb/locks-table.h"
#include "bdb/lock-tokens-table.h"
#include "bdb/node-origins-table.h"
#include "bdb/miscellaneous-table.h"
#include "bdb/checksum-reps-table.h"

#include "../libsvn_fs/fs-loader.h"
#include "private/svn_fs_util.h"


/* Checking for return values, and reporting errors.  */

/* Check that we're using the right Berkeley DB version. */
/* FIXME: This check should be abstracted into the DB back-end layer. */
static svn_error_t *
check_bdb_version(void)
{
  int major, minor, patch;

  db_version(&major, &minor, &patch);

  /* First, check that we're using a reasonably correct of Berkeley DB. */
  if ((major < SVN_FS_WANT_DB_MAJOR)
      || (major == SVN_FS_WANT_DB_MAJOR && minor < SVN_FS_WANT_DB_MINOR)
      || (major == SVN_FS_WANT_DB_MAJOR && minor == SVN_FS_WANT_DB_MINOR
          && patch < SVN_FS_WANT_DB_PATCH))
    return svn_error_createf(SVN_ERR_FS_GENERAL, 0,
                             _("Bad database version: got %d.%d.%d,"
                               " should be at least %d.%d.%d"),
                             major, minor, patch,
                             SVN_FS_WANT_DB_MAJOR,
                             SVN_FS_WANT_DB_MINOR,
                             SVN_FS_WANT_DB_PATCH);

  /* Now, check that the version we're running against is the same as
     the one we compiled with. */
  if (major != DB_VERSION_MAJOR || minor != DB_VERSION_MINOR)
    return svn_error_createf(SVN_ERR_FS_GENERAL, 0,
                             _("Bad database version:"
                               " compiled with %d.%d.%d,"
                               " running against %d.%d.%d"),
                             DB_VERSION_MAJOR,
                             DB_VERSION_MINOR,
                             DB_VERSION_PATCH,
                             major, minor, patch);
  return SVN_NO_ERROR;
}



/* Cleanup functions.  */

/* Close a database in the filesystem FS.
   DB_PTR is a pointer to the DB pointer in *FS to close.
   NAME is the name of the database, for use in error messages.  */
static svn_error_t *
cleanup_fs_db(svn_fs_t *fs, DB **db_ptr, const char *name)
{
  /* If the BDB environment is panicked, don't do anything, since
     attempting to close the database will fail anyway. */
  base_fs_data_t *bfd = fs->fsap_data;
  if (*db_ptr && !svn_fs_bdb__get_panic(bfd->bdb))
    {
      DB *db = *db_ptr;
      char *msg = apr_psprintf(fs->pool, "closing '%s' database", name);
      int db_err;

      *db_ptr = 0;
      db_err = db->close(db, 0);
      if (db_err == DB_RUNRECOVERY)
        {
          /* We can ignore DB_RUNRECOVERY errors from DB->close, but
             must set the panic flag in the environment baton.  The
             error will be propagated appropriately from
             svn_fs_bdb__close. */
          svn_fs_bdb__set_panic(bfd->bdb);
          db_err = 0;
        }

#if SVN_BDB_HAS_DB_INCOMPLETE
      /* We can ignore DB_INCOMPLETE on db->close and db->sync; it
       * just means someone else was using the db at the same time
       * we were.  See the Berkeley documentation at:
       * http://www.sleepycat.com/docs/ref/program/errorret.html#DB_INCOMPLETE
       * http://www.sleepycat.com/docs/api_c/db_close.html
       */
      if (db_err == DB_INCOMPLETE)
        db_err = 0;
#endif /* SVN_BDB_HAS_DB_INCOMPLETE */

      SVN_ERR(BDB_WRAP(fs, msg, db_err));
    }

  return SVN_NO_ERROR;
}

/* Close whatever Berkeley DB resources are allocated to FS.  */
static svn_error_t *
cleanup_fs(svn_fs_t *fs)
{
  base_fs_data_t *bfd = fs->fsap_data;
  bdb_env_baton_t *bdb = (bfd ? bfd->bdb : NULL);

  if (!bdb)
    return SVN_NO_ERROR;

  /* Close the databases.  */
  SVN_ERR(cleanup_fs_db(fs, &bfd->nodes, "nodes"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->revisions, "revisions"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->transactions, "transactions"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->copies, "copies"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->changes, "changes"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->representations, "representations"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->strings, "strings"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->uuids, "uuids"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->locks, "locks"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->lock_tokens, "lock-tokens"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->node_origins, "node-origins"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->checksum_reps, "checksum-reps"));
  SVN_ERR(cleanup_fs_db(fs, &bfd->miscellaneous, "miscellaneous"));

  /* Finally, close the environment.  */
  bfd->bdb = 0;
  {
    svn_error_t *err = svn_fs_bdb__close(bdb);
    if (err)
      return svn_error_createf
        (err->apr_err, err,
         _("Berkeley DB error for filesystem '%s'"
           " while closing environment:\n"),
         fs->path);
  }
  return SVN_NO_ERROR;
}

#if 0   /* Set to 1 for instrumenting. */
static void print_fs_stats(svn_fs_t *fs)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DB_TXN_STAT *t;
  DB_LOCK_STAT *l;
  int db_err;

  /* Print transaction statistics for this DB env. */
  if ((db_err = bfd->bdb->env->txn_stat(bfd->bdb->env, &t, 0)) != 0)
    fprintf(stderr, "Error running bfd->bdb->env->txn_stat(): %s",
            db_strerror(db_err));
  else
    {
      printf("*** DB transaction stats, right before closing env:\n");
      printf("   Number of transactions currently active: %d\n",
             t->st_nactive);
      printf("   Max number of active transactions at any one time: %d\n",
             t->st_maxnactive);
      printf("   Number of transactions that have begun: %d\n",
             t->st_nbegins);
      printf("   Number of transactions that have aborted: %d\n",
             t->st_naborts);
      printf("   Number of transactions that have committed: %d\n",
             t->st_ncommits);
      printf("   Number of times a thread was forced to wait: %d\n",
             t->st_region_wait);
      printf("   Number of times a thread didn't need to wait: %d\n",
             t->st_region_nowait);
      printf("*** End DB transaction stats.\n\n");
    }

  /* Print transaction statistics for this DB env. */
  if ((db_err = bfd->bdb->env->lock_stat(bfd->bdb->env, &l, 0)) != 0)
    fprintf(stderr, "Error running bfd->bdb->env->lock_stat(): %s",
            db_strerror(db_err));
  else
    {
      printf("*** DB lock stats, right before closing env:\n");
      printf("   The number of current locks: %d\n",
             l->st_nlocks);
      printf("   Max number of locks at any one time: %d\n",
             l->st_maxnlocks);
      printf("   Number of current lockers: %d\n",
             l->st_nlockers);
      printf("   Max number of lockers at any one time: %d\n",
             l->st_maxnlockers);
      printf("   Number of current objects: %d\n",
             l->st_nobjects);
      printf("   Max number of objects at any one time: %d\n",
             l->st_maxnobjects);
      printf("   Total number of locks requested: %d\n",
             l->st_nrequests);
      printf("   Total number of locks released: %d\n",
             l->st_nreleases);
      printf("   Total number of lock reqs failed because "
             "DB_LOCK_NOWAIT was set: %d\n", l->st_nnowaits);
      printf("   Total number of locks not immediately available "
             "due to conflicts: %d\n", l->st_nconflicts);
      printf("   Number of deadlocks detected: %d\n", l->st_ndeadlocks);
      printf("   Number of times a thread waited before "
             "obtaining the region lock: %d\n", l->st_region_wait);
      printf("   Number of times a thread didn't have to wait: %d\n",
             l->st_region_nowait);
      printf("*** End DB lock stats.\n\n");
    }

}
#else
#  define print_fs_stats(fs)
#endif /* 0/1 */

/* An APR pool cleanup function for a filesystem.  DATA must be a
   pointer to the filesystem to clean up.

   When the filesystem object's pool is freed, we want the resources
   held by Berkeley DB to go away, just like everything else.  So we
   register this cleanup function with the filesystem's pool, and let
   it take care of closing the databases, the environment, and any
   other DB objects we might be using.  APR calls this function before
   actually freeing the pool's memory.

   It's a pity that we can't return an svn_error_t object from an APR
   cleanup function.  For now, we return the rather generic
   SVN_ERR_FS_CLEANUP, and pass the real svn_error_t to the registered
   warning callback.  */

static apr_status_t
cleanup_fs_apr(void *data)
{
  svn_fs_t *fs = data;
  svn_error_t *err;

  print_fs_stats(fs);

  err = cleanup_fs(fs);
  if (! err)
    return APR_SUCCESS;

  /* Darn. An error during cleanup. Call the warning handler to
     try and do something "right" with this error. Note that
     the default will simply abort().  */
  (*fs->warning)(fs->warning_baton, err);

  svn_error_clear(err);

  return SVN_ERR_FS_CLEANUP;
}


static svn_error_t *
base_bdb_set_errcall(svn_fs_t *fs,
                     void (*db_errcall_fcn)(const char *errpfx, char *msg))
{
  base_fs_data_t *bfd = fs->fsap_data;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  bfd->bdb->error_info->user_callback = db_errcall_fcn;

  return SVN_NO_ERROR;
}


/* Write the DB_CONFIG file. */
static svn_error_t *
bdb_write_config(svn_fs_t *fs)
{
  const char *dbconfig_file_name =
    svn_dirent_join(fs->path, BDB_CONFIG_FILE, fs->pool);
  apr_file_t *dbconfig_file = NULL;
  int i;

  static const char dbconfig_contents[] =
    "# This is the configuration file for the Berkeley DB environment\n"
    "# used by your Subversion repository.\n"
    "# You must run 'svnadmin recover' whenever you modify this file,\n"
    "# for your changes to take effect.\n"
    "\n"
    "### Lock subsystem\n"
    "#\n"
    "# Make sure you read the documentation at:\n"
    "#\n"
    "#   http://docs.oracle.com/cd/E17076_02/html/programmer_reference/lock_max.html\n"
    "#\n"
    "# before tweaking these values.\n"
    "#\n"
    "set_lk_max_locks   2000\n"
    "set_lk_max_lockers 2000\n"
    "set_lk_max_objects 2000\n"
    "\n"
    "### Log file subsystem\n"
    "#\n"
    "# Make sure you read the documentation at:\n"
    "#\n"
    "#   http://docs.oracle.com/cd/E17076_02/html/api_reference/C/envset_lg_bsize.html\n"
    "#   http://docs.oracle.com/cd/E17076_02/html/api_reference/C/envset_lg_max.html\n"
    "#   http://docs.oracle.com/cd/E17076_02/html/programmer_reference/log_limits.html\n"
    "#\n"
    "# Increase the size of the in-memory log buffer from the default\n"
    "# of 32 Kbytes to 256 Kbytes.  Decrease the log file size from\n"
    "# 10 Mbytes to 1 Mbyte.  This will help reduce the amount of disk\n"
    "# space required for hot backups.  The size of the log file must be\n"
    "# at least four times the size of the in-memory log buffer.\n"
    "#\n"
    "# Note: Decreasing the in-memory buffer size below 256 Kbytes will hurt\n"
    "# hurt commit performance. For details, see:\n"
    "#\n"
    "#   http://svn.haxx.se/dev/archive-2002-02/0141.shtml\n"
    "#\n"
    "set_lg_bsize     262144\n"
    "set_lg_max      1048576\n"
    "#\n"
    "# If you see \"log region out of memory\" errors, bump lg_regionmax.\n"
    "# For more information, see:\n"
    "#\n"
    "#   http://docs.oracle.com/cd/E17076_02/html/programmer_reference/log_config.html\n"
    "#   http://svn.haxx.se/users/archive-2004-10/1000.shtml\n"
    "#\n"
    "set_lg_regionmax 131072\n"
    "#\n"
    /* ### Configure this with "svnadmin create --bdb-cache-size" */
    "# The default cache size in BDB is only 256k. As explained in\n"
    "# http://svn.haxx.se/dev/archive-2004-12/0368.shtml, this is too\n"
    "# small for most applications. Bump this number if \"db_stat -m\"\n"
    "# shows too many cache misses.\n"
    "#\n"
    "set_cachesize    0 1048576 1\n";

  /* Run-time configurable options.
     Each option set consists of a minimum required BDB version, a
     config hash key, a header, an inactive form and an active
     form. We always write the header; then, depending on the
     run-time configuration and the BDB version we're compiling
     against, we write either the active or inactive form of the
     value. */
  static const struct
  {
    int bdb_major;
    int bdb_minor;
    const char *config_key;
    const char *header;
    const char *inactive;
    const char *active;
  } dbconfig_options[] = {
    /* Controlled by "svnadmin create --bdb-txn-nosync" */
    { 4, 0, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
      /* header */
      "#\n"
      "# Disable fsync of log files on transaction commit. Read the\n"
      "# documentation about DB_TXN_NOSYNC at:\n"
      "#\n"
      "#   http://docs.oracle.com/cd/E17076_02/html/programmer_reference/log_config.html\n"
      "#\n"
      "# [requires Berkeley DB 4.0]\n"
      "#\n",
      /* inactive */
      "#set_flags DB_TXN_NOSYNC\n",
      /* active */
      "set_flags DB_TXN_NOSYNC\n" },
    /* Controlled by "svnadmin create --bdb-log-keep" */
    { 4, 2, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE,
      /* header */
      "#\n"
      "# Enable automatic removal of unused transaction log files.\n"
      "# Read the documentation about DB_LOG_AUTOREMOVE at:\n"
      "#\n"
      "#   http://docs.oracle.com/cd/E17076_02/html/programmer_reference/log_config.html\n"
      "#\n"
      "# [requires Berkeley DB 4.2]\n"
      "#\n",
      /* inactive */
      "#set_flags DB_LOG_AUTOREMOVE\n",
      /* active */
      "set_flags DB_LOG_AUTOREMOVE\n" },
  };
  static const int dbconfig_options_length =
    sizeof(dbconfig_options)/sizeof(*dbconfig_options);


  SVN_ERR(svn_io_file_open(&dbconfig_file, dbconfig_file_name,
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                           fs->pool));

  SVN_ERR(svn_io_file_write_full(dbconfig_file, dbconfig_contents,
                                 sizeof(dbconfig_contents) - 1, NULL,
                                 fs->pool));

  /* Write the variable DB_CONFIG flags. */
  for (i = 0; i < dbconfig_options_length; ++i)
    {
      void *value = NULL;
      const char *choice;

      if (fs->config)
        {
          value = svn_hash_gets(fs->config, dbconfig_options[i].config_key);
        }

      SVN_ERR(svn_io_file_write_full(dbconfig_file,
                                     dbconfig_options[i].header,
                                     strlen(dbconfig_options[i].header),
                                     NULL, fs->pool));

      if (((DB_VERSION_MAJOR == dbconfig_options[i].bdb_major
            && DB_VERSION_MINOR >= dbconfig_options[i].bdb_minor)
           || DB_VERSION_MAJOR > dbconfig_options[i].bdb_major)
          && value != NULL && strcmp(value, "0") != 0)
        choice = dbconfig_options[i].active;
      else
        choice = dbconfig_options[i].inactive;

      SVN_ERR(svn_io_file_write_full(dbconfig_file, choice, strlen(choice),
                                     NULL, fs->pool));
    }

  return svn_io_file_close(dbconfig_file, fs->pool);
}

static svn_error_t *
base_bdb_refresh_revision(svn_fs_t *fs,
                          apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
base_bdb_info_format(int *fs_format,
                     svn_version_t **supports_version,
                     svn_fs_t *fs,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  base_fs_data_t *bfd = fs->fsap_data;

  *fs_format = bfd->format;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 0;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  switch (bfd->format)
    {
    case 1:
      break;
    case 2:
      (*supports_version)->minor = 4;
      break;
    case 3:
      (*supports_version)->minor = 5;
      break;
    case 4:
      (*supports_version)->minor = 6;
      break;
#ifdef SVN_DEBUG
# if SVN_FS_BASE__FORMAT_NUMBER != 4
#  error "Need to add a 'case' statement here"
# endif
#endif
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
base_bdb_info_config_files(apr_array_header_t **files,
                           svn_fs_t *fs,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  *files = apr_array_make(result_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(*files, const char *) = svn_dirent_join(fs->path,
                                                         BDB_CONFIG_FILE,
                                                         result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
base_bdb_verify_root(svn_fs_root_t *root,
                     apr_pool_t *scratch_pool)
{
  /* Verifying is currently a no op for BDB. */
  return SVN_NO_ERROR;
}

static svn_error_t *
base_bdb_freeze(svn_fs_t *fs,
                svn_fs_freeze_func_t freeze_func,
                void *freeze_baton,
                apr_pool_t *pool)
{
  SVN__NOT_IMPLEMENTED();
}


/* Creating a new filesystem */

static fs_vtable_t fs_vtable = {
  svn_fs_base__youngest_rev,
  base_bdb_refresh_revision,
  svn_fs_base__revision_prop,
  svn_fs_base__revision_proplist,
  svn_fs_base__change_rev_prop,
  svn_fs_base__set_uuid,
  svn_fs_base__revision_root,
  svn_fs_base__begin_txn,
  svn_fs_base__open_txn,
  svn_fs_base__purge_txn,
  svn_fs_base__list_transactions,
  svn_fs_base__deltify,
  svn_fs_base__lock,
  svn_fs_base__generate_lock_token,
  svn_fs_base__unlock,
  svn_fs_base__get_lock,
  svn_fs_base__get_locks,
  base_bdb_info_format,
  base_bdb_info_config_files,
  NULL /* info_fsap */,
  base_bdb_verify_root,
  base_bdb_freeze,
  base_bdb_set_errcall,
};

/* Where the format number is stored. */
#define FORMAT_FILE   "format"

/* Depending on CREATE, create or open the environment and databases
   for filesystem FS in PATH. */
static svn_error_t *
open_databases(svn_fs_t *fs,
               svn_boolean_t create,
               int format,
               const char *path)
{
  base_fs_data_t *bfd;

  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  bfd = apr_pcalloc(fs->pool, sizeof(*bfd));
  fs->vtable = &fs_vtable;
  fs->fsap_data = bfd;

  /* Initialize the fs's path. */
  fs->path = apr_pstrdup(fs->pool, path);

  if (create)
    SVN_ERR(bdb_write_config(fs));

  /* Create the Berkeley DB environment.  */
  {
    svn_error_t *err = svn_fs_bdb__open(&(bfd->bdb), path,
                                        SVN_BDB_STANDARD_ENV_FLAGS,
                                        0666, fs->pool);
    if (err)
      {
        if (create)
          return svn_error_createf
            (err->apr_err, err,
             _("Berkeley DB error for filesystem '%s'"
               " while creating environment:\n"),
             fs->path);
        else
          return svn_error_createf
            (err->apr_err, err,
             _("Berkeley DB error for filesystem '%s'"
               " while opening environment:\n"),
             fs->path);
      }
  }

  /* We must register the FS cleanup function *after* opening the
     environment, so that it's run before the environment baton
     cleanup. */
  apr_pool_cleanup_register(fs->pool, fs, cleanup_fs_apr,
                            apr_pool_cleanup_null);


  /* Create the databases in the environment.  */
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'nodes' table")
                        : N_("opening 'nodes' table")),
                   svn_fs_bdb__open_nodes_table(&bfd->nodes,
                                                bfd->bdb->env,
                                                create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'revisions' table")
                        : N_("opening 'revisions' table")),
                   svn_fs_bdb__open_revisions_table(&bfd->revisions,
                                                    bfd->bdb->env,
                                                    create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'transactions' table")
                        : N_("opening 'transactions' table")),
                   svn_fs_bdb__open_transactions_table(&bfd->transactions,
                                                       bfd->bdb->env,
                                                       create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'copies' table")
                        : N_("opening 'copies' table")),
                   svn_fs_bdb__open_copies_table(&bfd->copies,
                                                 bfd->bdb->env,
                                                 create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'changes' table")
                        : N_("opening 'changes' table")),
                   svn_fs_bdb__open_changes_table(&bfd->changes,
                                                  bfd->bdb->env,
                                                  create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'representations' table")
                        : N_("opening 'representations' table")),
                   svn_fs_bdb__open_reps_table(&bfd->representations,
                                               bfd->bdb->env,
                                               create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'strings' table")
                        : N_("opening 'strings' table")),
                   svn_fs_bdb__open_strings_table(&bfd->strings,
                                                  bfd->bdb->env,
                                                  create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'uuids' table")
                        : N_("opening 'uuids' table")),
                   svn_fs_bdb__open_uuids_table(&bfd->uuids,
                                                bfd->bdb->env,
                                                create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'locks' table")
                        : N_("opening 'locks' table")),
                   svn_fs_bdb__open_locks_table(&bfd->locks,
                                                bfd->bdb->env,
                                                create)));
  SVN_ERR(BDB_WRAP(fs, (create
                        ? N_("creating 'lock-tokens' table")
                        : N_("opening 'lock-tokens' table")),
                   svn_fs_bdb__open_lock_tokens_table(&bfd->lock_tokens,
                                                      bfd->bdb->env,
                                                      create)));

  if (format >= SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT)
    {
      SVN_ERR(BDB_WRAP(fs, (create
                            ? N_("creating 'node-origins' table")
                            : N_("opening 'node-origins' table")),
                       svn_fs_bdb__open_node_origins_table(&bfd->node_origins,
                                                           bfd->bdb->env,
                                                           create)));
    }

  if (format >= SVN_FS_BASE__MIN_MISCELLANY_FORMAT)
    {
      SVN_ERR(BDB_WRAP(fs, (create
                            ? N_("creating 'miscellaneous' table")
                            : N_("opening 'miscellaneous' table")),
                       svn_fs_bdb__open_miscellaneous_table(&bfd->miscellaneous,
                                                            bfd->bdb->env,
                                                            create)));
    }

  if (format >= SVN_FS_BASE__MIN_REP_SHARING_FORMAT)
    {
      SVN_ERR(BDB_WRAP(fs, (create
                            ? N_("creating 'checksum-reps' table")
                            : N_("opening 'checksum-reps' table")),
                       svn_fs_bdb__open_checksum_reps_table(&bfd->checksum_reps,
                                                            bfd->bdb->env,
                                                            create)));
    }

  return SVN_NO_ERROR;
}


/* Called by functions that initialize an svn_fs_t struct, after that
   initialization is done, to populate svn_fs_t->uuid. */
static svn_error_t *
populate_opened_fs(svn_fs_t *fs, apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_fs_base__populate_uuid(fs, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
base_create(svn_fs_t *fs,
            const char *path,
            svn_mutex__t *common_pool_lock,
            apr_pool_t *scratch_pool,
            apr_pool_t *common_pool)
{
  int format = SVN_FS_BASE__FORMAT_NUMBER;
  svn_error_t *svn_err;

  /* See if compatibility with older versions was explicitly requested. */
  if (fs->config)
    {
      svn_version_t *compatible_version;
      SVN_ERR(svn_fs__compatible_version(&compatible_version, fs->config,
                                         scratch_pool));

      /* select format number */
      switch(compatible_version->minor)
        {
          case 0:
          case 1:
          case 2:
          case 3: format = 1;
                  break;

          case 4: format = 2;
                  break;

          case 5: format = 3;
                  break;

          default:format = SVN_FS_BASE__FORMAT_NUMBER;
        }
    }

  /* Create the environment and databases. */
  svn_err = open_databases(fs, TRUE, format, path);
  if (svn_err) goto error;

  /* Initialize the DAG subsystem. */
  svn_err = svn_fs_base__dag_init_fs(fs);
  if (svn_err) goto error;

  /* This filesystem is ready.  Stamp it with a format number. */
  svn_err = svn_io_write_version_file(svn_dirent_join(fs->path, FORMAT_FILE,
                                                      scratch_pool),
                                      format, scratch_pool);
  if (svn_err) goto error;

  ((base_fs_data_t *) fs->fsap_data)->format = format;

  SVN_ERR(populate_opened_fs(fs, scratch_pool));
  return SVN_NO_ERROR;

error:
  return svn_error_compose_create(svn_err,
                                  svn_error_trace(cleanup_fs(fs)));
}


/* Gaining access to an existing Berkeley DB-based filesystem.  */

svn_error_t *
svn_fs_base__test_required_feature_format(svn_fs_t *fs,
                                          const char *feature,
                                          int requires)
{
  base_fs_data_t *bfd = fs->fsap_data;
  if (bfd->format < requires)
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("The '%s' feature requires version %d of the filesystem schema; "
         "filesystem '%s' uses only version %d"),
       feature, requires, fs->path, bfd->format);
  return SVN_NO_ERROR;
}

/* Return the error SVN_ERR_FS_UNSUPPORTED_FORMAT if FS's format
   number is not the same as the format number supported by this
   Subversion. */
static svn_error_t *
check_format(int format)
{
  /* We currently support any format less than the compiled format number
     simultaneously.  */
  if (format <= SVN_FS_BASE__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  return svn_error_createf(
        SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
        _("Expected FS format '%d'; found format '%d'"),
        SVN_FS_BASE__FORMAT_NUMBER, format);
}

static svn_error_t *
base_open(svn_fs_t *fs,
          const char *path,
          svn_mutex__t *common_pool_lock,
          apr_pool_t *scratch_pool,
          apr_pool_t *common_pool)
{
  int format;
  svn_error_t *svn_err;
  svn_boolean_t write_format_file = FALSE;

  /* Read the FS format number. */
  svn_err = svn_io_read_version_file(&format,
                                     svn_dirent_join(path, FORMAT_FILE,
                                                     scratch_pool),
                                     scratch_pool);
  if (svn_err && APR_STATUS_IS_ENOENT(svn_err->apr_err))
    {
      /* Pre-1.2 filesystems did not have a format file (you could say
         they were format "0"), so they get upgraded on the fly.
         However, we stopped "upgrading on the fly" in 1.5, so older
         filesystems should only be bumped to 1.3, which is format "1". */
      svn_error_clear(svn_err);
      svn_err = SVN_NO_ERROR;
      format = 1;
      write_format_file = TRUE;
    }
  else if (svn_err)
    goto error;

  /* Create the environment and databases. */
  svn_err = open_databases(fs, FALSE, format, path);
  if (svn_err) goto error;

  ((base_fs_data_t *) fs->fsap_data)->format = format;
  SVN_ERR(check_format(format));

  /* If we lack a format file, write one. */
  if (write_format_file)
    {
      svn_err = svn_io_write_version_file(svn_dirent_join(path, FORMAT_FILE,
                                                        scratch_pool),
                                          format, scratch_pool);
      if (svn_err) goto error;
    }

  SVN_ERR(populate_opened_fs(fs, scratch_pool));
  return SVN_NO_ERROR;

 error:
  return svn_error_compose_create(svn_err,
                                  svn_error_trace(cleanup_fs(fs)));
}


/* Running recovery on a Berkeley DB-based filesystem.  */


/* Recover a database at PATH. Perform catastrophic recovery if FATAL
   is TRUE. Use POOL for temporary allocation. */
static svn_error_t *
bdb_recover(const char *path, svn_boolean_t fatal, apr_pool_t *pool)
{
  bdb_env_baton_t *bdb;

  /* Here's the comment copied from db_recover.c:

     Initialize the environment -- we don't actually do anything
     else, that all that's needed to run recovery.

     Note that we specify a private environment, as we're about to
     create a region, and we don't want to leave it around.  If we
     leave the region around, the application that should create it
     will simply join it instead, and will then be running with
     incorrectly sized (and probably terribly small) caches.  */

  /* Note that since we're using a private environment, we shoudl
     /not/ initialize locking. We want the environment files to go
     away. */

  SVN_ERR(svn_fs_bdb__open(&bdb, path,
                           ((fatal ? DB_RECOVER_FATAL : DB_RECOVER)
                            | SVN_BDB_PRIVATE_ENV_FLAGS),
                           0666, pool));
  return svn_fs_bdb__close(bdb);
}

static svn_error_t *
base_open_for_recovery(svn_fs_t *fs,
                       const char *path,
                       svn_mutex__t *common_pool_lock,
                       apr_pool_t *pool,
                       apr_pool_t *common_pool)
{
  /* Just stash the path in the fs pointer - it's all we really need. */
  fs->path = apr_pstrdup(fs->pool, path);

  return SVN_NO_ERROR;
}

static svn_error_t *
base_upgrade(svn_fs_t *fs,
             const char *path,
             svn_fs_upgrade_notify_t notify_func,
             void *notify_baton,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_mutex__t *common_pool_lock,
             apr_pool_t *pool,
             apr_pool_t *common_pool)
{
  const char *version_file_path;
  int old_format_number;
  svn_error_t *err;

  version_file_path = svn_dirent_join(path, FORMAT_FILE, pool);

  /* Read the old number so we've got it on hand later on. */
  err = svn_io_read_version_file(&old_format_number, version_file_path, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* Pre-1.2 filesystems do not have a 'format' file. */
      old_format_number = 0;
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  SVN_ERR(err);
  SVN_ERR(check_format(old_format_number));

  /* Bump the format file's stored version number. */
  SVN_ERR(svn_io_write_version_file(version_file_path,
                                    SVN_FS_BASE__FORMAT_NUMBER, pool));
  if (notify_func)
    SVN_ERR(notify_func(notify_baton, SVN_FS_BASE__FORMAT_NUMBER,
                        svn_fs_upgrade_format_bumped, pool));

  /* Check and see if we need to record the "bump" revision. */
  if (old_format_number < SVN_FS_BASE__MIN_FORWARD_DELTAS_FORMAT)
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      svn_revnum_t youngest_rev;
      const char *value;

      /* Open the filesystem in a subpool (so we can control its
         closure) and do our fiddling.

         NOTE: By using base_open() here instead of open_databases(),
         we will end up re-reading the format file that we just wrote.
         But it's better to use the existing encapsulation of "opening
         the filesystem" rather than duplicating (or worse, partially
         duplicating) that logic here.  */
      SVN_ERR(base_open(fs, path, common_pool_lock, subpool, common_pool));

      /* Fetch the youngest rev, and record it */
      SVN_ERR(svn_fs_base__youngest_rev(&youngest_rev, fs, subpool));
      value = apr_psprintf(subpool, "%ld", youngest_rev);
      SVN_ERR(svn_fs_base__miscellaneous_set
              (fs, SVN_FS_BASE__MISC_FORWARD_DELTA_UPGRADE,
               value, subpool));
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
base_verify(svn_fs_t *fs, const char *path,
            svn_revnum_t start,
            svn_revnum_t end,
            svn_fs_progress_notify_func_t notify_func,
            void *notify_baton,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_mutex__t *common_pool_lock,
            apr_pool_t *pool,
            apr_pool_t *common_pool)
{
  /* Verifying is currently a no op for BDB. */
  return SVN_NO_ERROR;
}

static svn_error_t *
base_bdb_recover(svn_fs_t *fs,
                 svn_cancel_func_t cancel_func, void *cancel_baton,
                 apr_pool_t *pool)
{
  /* The fs pointer is a fake created in base_open_for_recovery above.
     We only care about the path. */
  return bdb_recover(fs->path, FALSE, pool);
}

static svn_error_t *
base_bdb_pack(svn_fs_t *fs,
              const char *path,
              svn_fs_pack_notify_t notify_func,
              void *notify_baton,
              svn_cancel_func_t cancel,
              void *cancel_baton,
              svn_mutex__t *common_pool_lock,
              apr_pool_t *pool,
              apr_pool_t *common_pool)
{
  /* Packing is currently a no op for BDB. */
  return SVN_NO_ERROR;
}



/* Running the 'archive' command on a Berkeley DB-based filesystem.  */


static svn_error_t *
base_bdb_logfiles(apr_array_header_t **logfiles,
                  const char *path,
                  svn_boolean_t only_unused,
                  apr_pool_t *pool)
{
  bdb_env_baton_t *bdb;
  char **filelist;
  char **filename;
  u_int32_t flags = only_unused ? 0 : DB_ARCH_LOG;

  *logfiles = apr_array_make(pool, 4, sizeof(const char *));

  SVN_ERR(svn_fs_bdb__open(&bdb, path,
                           SVN_BDB_STANDARD_ENV_FLAGS,
                           0666, pool));
  SVN_BDB_ERR(bdb, bdb->env->log_archive(bdb->env, &filelist, flags));

  if (filelist == NULL)
    return svn_fs_bdb__close(bdb);

  for (filename = filelist; *filename != NULL; ++filename)
    {
      APR_ARRAY_PUSH(*logfiles, const char *) = apr_pstrdup(pool, *filename);
    }

  free(filelist);

  return svn_fs_bdb__close(bdb);
}



/* Copying a live Berkeley DB-base filesystem.  */

/**
 * Delete all unused log files from DBD enviroment at @a live_path that exist
 * in @a backup_path.
 */
static svn_error_t *
svn_fs_base__clean_logs(const char *live_path,
                        const char *backup_path,
                        apr_pool_t *pool)
{
  apr_array_header_t *logfiles;

  SVN_ERR(base_bdb_logfiles(&logfiles,
                            live_path,
                            TRUE,        /* Only unused logs */
                            pool));

  {  /* Process unused logs from live area */
    int idx;
    apr_pool_t *subpool = svn_pool_create(pool);

    /* Process log files. */
    for (idx = 0; idx < logfiles->nelts; idx++)
      {
        const char *log_file = APR_ARRAY_IDX(logfiles, idx, const char *);
        const char *live_log_path;
        const char *backup_log_path;

        svn_pool_clear(subpool);
        live_log_path = svn_dirent_join(live_path, log_file, subpool);
        backup_log_path = svn_dirent_join(backup_path, log_file, subpool);

        { /* Compare files. No point in using MD5 and wasting CPU cycles as we
             got full copies of both logs */

          svn_boolean_t files_match = FALSE;
          svn_node_kind_t kind;

          /* Check to see if there is a corresponding log file in the backup
             directory */
          SVN_ERR(svn_io_check_path(backup_log_path, &kind, pool));

          /* If the copy of the log exists, compare them */
          if (kind == svn_node_file)
            SVN_ERR(svn_io_files_contents_same_p(&files_match,
                                                 live_log_path,
                                                 backup_log_path,
                                                 subpool));

          /* If log files do not match, go to the next log file. */
          if (!files_match)
            continue;
        }

        SVN_ERR(svn_io_remove_file2(live_log_path, FALSE, subpool));
      }

    svn_pool_destroy(subpool);
  }

  return SVN_NO_ERROR;
}


/* DB_ENV->get_flags() and DB->get_pagesize() don't exist prior to
   Berkeley DB 4.2. */
#if SVN_BDB_VERSION_AT_LEAST(4, 2)

/* Open the BDB environment at PATH and compare its configuration
   flags with FLAGS.  If every flag in FLAGS is set in the
   environment, then set *MATCH to true.  Else set *MATCH to false. */
static svn_error_t *
check_env_flags(svn_boolean_t *match,
                u_int32_t flags,
                const char *path,
                apr_pool_t *pool)
{
  bdb_env_baton_t *bdb;
#if SVN_BDB_VERSION_AT_LEAST(4, 7)
  int flag_state;
#else
  u_int32_t envflags;
#endif

  SVN_ERR(svn_fs_bdb__open(&bdb, path,
                           SVN_BDB_STANDARD_ENV_FLAGS,
                           0666, pool));
#if SVN_BDB_VERSION_AT_LEAST(4, 7)
  SVN_BDB_ERR(bdb, bdb->env->log_get_config(bdb->env, flags, &flag_state));
#else
  SVN_BDB_ERR(bdb, bdb->env->get_flags(bdb->env, &envflags));
#endif

  SVN_ERR(svn_fs_bdb__close(bdb));

#if SVN_BDB_VERSION_AT_LEAST(4, 7)
  if (flag_state == 0)
#else
  if (flags & envflags)
#endif
    *match = TRUE;
  else
    *match = FALSE;

  return SVN_NO_ERROR;
}


/* Set *PAGESIZE to the size of pages used to hold items in the
   database environment located at PATH.
*/
static svn_error_t *
get_db_pagesize(u_int32_t *pagesize,
                const char *path,
                apr_pool_t *pool)
{
  bdb_env_baton_t *bdb;
  DB *nodes_table;

  SVN_ERR(svn_fs_bdb__open(&bdb, path,
                           SVN_BDB_STANDARD_ENV_FLAGS,
                           0666, pool));

  /* ### We're only asking for the pagesize on the 'nodes' table.
         Is this enough?  We never call DB->set_pagesize() on any of
         our tables, so presumably BDB is using the same default
         pagesize for all our databases, right? */
  SVN_BDB_ERR(bdb, svn_fs_bdb__open_nodes_table(&nodes_table, bdb->env,
                                                FALSE));
  SVN_BDB_ERR(bdb, nodes_table->get_pagesize(nodes_table, pagesize));
  SVN_BDB_ERR(bdb, nodes_table->close(nodes_table, 0));

  return svn_fs_bdb__close(bdb);
}
#endif /* SVN_BDB_VERSION_AT_LEAST(4, 2) */


/* Copy FILENAME from SRC_DIR to DST_DIR in byte increments of size
   CHUNKSIZE.  The read/write buffer of size CHUNKSIZE will be
   allocated in POOL.  If ALLOW_MISSING is set, we won't make a fuss
   if FILENAME isn't found in SRC_DIR; otherwise, we will.  */
static svn_error_t *
copy_db_file_safely(const char *src_dir,
                    const char *dst_dir,
                    const char *filename,
                    u_int32_t chunksize,
                    svn_boolean_t allow_missing,
                    apr_pool_t *pool)
{
  apr_file_t *s = NULL, *d = NULL;  /* init to null important for APR */
  const char *file_src_path = svn_dirent_join(src_dir, filename, pool);
  const char *file_dst_path = svn_dirent_join(dst_dir, filename, pool);
  svn_error_t *err;
  char *buf;

  /* Open source file.  If it's missing and that's allowed, there's
     nothing more to do here. */
  err = svn_io_file_open(&s, file_src_path,
                         (APR_READ | APR_LARGEFILE),
                         APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err) && allow_missing)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* Open destination file. */
  SVN_ERR(svn_io_file_open(&d, file_dst_path, (APR_WRITE | APR_CREATE |
                                               APR_LARGEFILE),
                           APR_OS_DEFAULT, pool));

  /* Allocate our read/write buffer. */
  buf = apr_palloc(pool, chunksize);

  /* Copy bytes till the cows come home. */
  while (1)
    {
      apr_size_t bytes_this_time = chunksize;
      svn_error_t *read_err, *write_err;

      /* Read 'em. */
      if ((read_err = svn_io_file_read(s, buf, &bytes_this_time, pool)))
        {
          if (APR_STATUS_IS_EOF(read_err->apr_err))
            svn_error_clear(read_err);
          else
            {
              svn_error_clear(svn_io_file_close(s, pool));
              svn_error_clear(svn_io_file_close(d, pool));
              return read_err;
            }
        }

      /* Write 'em. */
      if ((write_err = svn_io_file_write_full(d, buf, bytes_this_time, NULL,
                                              pool)))
        {
          svn_error_clear(svn_io_file_close(s, pool));
          svn_error_clear(svn_io_file_close(d, pool));
          return write_err;
        }

      /* read_err is either NULL, or a dangling pointer - but it is only a
         dangling pointer if it used to be an EOF error. */
      if (read_err)
        {
          SVN_ERR(svn_io_file_close(s, pool));
          SVN_ERR(svn_io_file_close(d, pool));
          break;  /* got EOF on read, all files closed, all done. */
        }
    }

  return SVN_NO_ERROR;
}




static svn_error_t *
base_hotcopy(svn_fs_t *src_fs,
             svn_fs_t *dst_fs,
             const char *src_path,
             const char *dest_path,
             svn_boolean_t clean_logs,
             svn_boolean_t incremental,
             svn_fs_hotcopy_notify_t notify_func,
             void *notify_baton,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_mutex__t *common_pool_lock,
             apr_pool_t *pool,
             apr_pool_t *common_pool)
{
  svn_error_t *err;
  u_int32_t pagesize;
  svn_boolean_t log_autoremove = FALSE;
  int format;

  if (incremental)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("BDB repositories do not support incremental "
                               "hotcopy"));

  /* Check the FS format number to be certain that we know how to
     hotcopy this FS.  Pre-1.2 filesystems did not have a format file (you
     could say they were format "0"), so we will error here.  This is not
     optimal, but since this has been the case since 1.2.0, and no one has
     complained, it apparently isn't much of a concern.  (We did not check
     the 'format' file in 1.2.x, but we did blindly try to copy 'locks',
     which would have errored just the same.)  */
  SVN_ERR(svn_io_read_version_file(
          &format, svn_dirent_join(src_path, FORMAT_FILE, pool), pool));
  SVN_ERR(check_format(format));

  /* If using Berkeley DB 4.2 or later, note whether the DB_LOG_AUTO_REMOVE
     feature is on.  If it is, we have a potential race condition:
     another process might delete a logfile while we're in the middle
     of copying all the logfiles.  (This is not a huge deal; at worst,
     the hotcopy fails with a file-not-found error.) */
#if SVN_BDB_VERSION_AT_LEAST(4, 2)
  err = check_env_flags(&log_autoremove,
#if SVN_BDB_VERSION_AT_LEAST(4, 7)
                          DB_LOG_AUTO_REMOVE,
 /* DB_LOG_AUTO_REMOVE was named DB_LOG_AUTOREMOVE before Berkeley DB 4.7. */
#else
                          DB_LOG_AUTOREMOVE,
#endif
                          src_path, pool);
#endif
  SVN_ERR(err);

  /* Copy the DB_CONFIG file. */
  SVN_ERR(svn_io_dir_file_copy(src_path, dest_path, "DB_CONFIG", pool));

  /* In order to copy the database files safely and atomically, we
     must copy them in chunks which are multiples of the page-size
     used by BDB.  See sleepycat docs for details, or svn issue #1818. */
#if SVN_BDB_VERSION_AT_LEAST(4, 2)
  SVN_ERR(get_db_pagesize(&pagesize, src_path, pool));
  if (pagesize < SVN__STREAM_CHUNK_SIZE)
    {
      /* use the largest multiple of BDB pagesize we can. */
      int multiple = SVN__STREAM_CHUNK_SIZE / pagesize;
      pagesize *= multiple;
    }
#else
  /* default to 128K chunks, which should be safe.
     BDB almost certainly uses a power-of-2 pagesize. */
  pagesize = (4096 * 32);
#endif

  /* Copy the databases.  */
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "nodes", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "transactions", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "revisions", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "copies", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "changes", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "representations", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "strings", pagesize, FALSE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "uuids", pagesize, TRUE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "locks", pagesize, TRUE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "lock-tokens", pagesize, TRUE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "node-origins", pagesize, TRUE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "checksum-reps", pagesize, TRUE, pool));
  SVN_ERR(copy_db_file_safely(src_path, dest_path,
                              "miscellaneous", pagesize, TRUE, pool));

  {
    apr_array_header_t *logfiles;
    int idx;
    apr_pool_t *subpool;

    SVN_ERR(base_bdb_logfiles(&logfiles,
                              src_path,
                              FALSE,   /* All logs */
                              pool));

    /* Process log files. */
    subpool = svn_pool_create(pool);
    for (idx = 0; idx < logfiles->nelts; idx++)
      {
        svn_pool_clear(subpool);
        err = svn_io_dir_file_copy(src_path, dest_path,
                                   APR_ARRAY_IDX(logfiles, idx,
                                                 const char *),
                                   subpool);
        if (err)
          {
            if (log_autoremove)
              return
                svn_error_quick_wrap
                (err,
                 _("Error copying logfile;  the DB_LOG_AUTOREMOVE feature\n"
                   "may be interfering with the hotcopy algorithm.  If\n"
                   "the problem persists, try deactivating this feature\n"
                   "in DB_CONFIG"));
            else
              return svn_error_trace(err);
          }
      }
    svn_pool_destroy(subpool);
  }

  /* Since this is a copy we will have exclusive access to the repository. */
  err = bdb_recover(dest_path, TRUE, pool);
  if (err)
    {
      if (log_autoremove)
        return
          svn_error_quick_wrap
          (err,
           _("Error running catastrophic recovery on hotcopy;  the\n"
             "DB_LOG_AUTOREMOVE feature may be interfering with the\n"
             "hotcopy algorithm.  If the problem persists, try deactivating\n"
             "this feature in DB_CONFIG"));
      else
        return svn_error_trace(err);
    }

  /* Only now that the hotcopied filesystem is complete,
     stamp it with a format file. */
  SVN_ERR(svn_io_write_version_file(
             svn_dirent_join(dest_path, FORMAT_FILE, pool), format, pool));

  if (clean_logs)
    SVN_ERR(svn_fs_base__clean_logs(src_path, dest_path, pool));

  return SVN_NO_ERROR;
}



/* Deleting a Berkeley DB-based filesystem.  */


static svn_error_t *
base_delete_fs(const char *path,
               apr_pool_t *pool)
{
  /* First, use the Berkeley DB library function to remove any shared
     memory segments.  */
  SVN_ERR(svn_fs_bdb__remove(path, pool));

  /* Remove the environment directory. */
  return svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
}

static const svn_version_t *
base_version(void)
{
  SVN_VERSION_BODY;
}

static const char *
base_get_description(void)
{
  return _("Module for working with a Berkeley DB repository.");
}

static svn_error_t *
base_set_svn_fs_open(svn_fs_t *fs,
                     svn_error_t *(*svn_fs_open_)(svn_fs_t **,
                                                  const char *,
                                                  apr_hash_t *,
                                                  apr_pool_t *,
                                                  apr_pool_t *))
{
  return SVN_NO_ERROR;
}


/* Base FS library vtable, used by the FS loader library. */
static fs_library_vtable_t library_vtable = {
  base_version,
  base_create,
  base_open,
  base_open_for_recovery,
  base_upgrade,
  base_verify,
  base_delete_fs,
  base_hotcopy,
  base_get_description,
  base_bdb_recover,
  base_bdb_pack,
  base_bdb_logfiles,
  svn_fs_base__id_parse,
  base_set_svn_fs_open,
  NULL /* info_fsap_dup */
};

svn_error_t *
svn_fs_base__init(const svn_version_t *loader_version,
                  fs_library_vtable_t **vtable, apr_pool_t* common_pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_fs_util", svn_fs_util__version },
      { NULL, NULL }
    };

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The FS loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported FS loader version (%d) for bdb"),
                             loader_version->major);
  SVN_ERR(svn_ver_check_list2(base_version(), checklist, svn_ver_equal));
  SVN_ERR(check_bdb_version());
  SVN_ERR(svn_fs_bdb__init(common_pool));

  *vtable = &library_vtable;
  return SVN_NO_ERROR;
}
