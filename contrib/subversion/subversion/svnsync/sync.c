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

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_auth.h"
#include "svn_opt.h"
#include "svn_ra.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_string.h"

#include "private/svn_string_private.h"

#include "sync.h"

#include "svn_private_config.h"

#include <apr_network_io.h>
#include <apr_signal.h>
#include <apr_uuid.h>


/* Normalize the encoding and line ending style of *STR, so that it contains
 * only LF (\n) line endings and is encoded in UTF-8. After return, *STR may
 * point at a new svn_string_t* allocated in RESULT_POOL.
 *
 * If SOURCE_PROP_ENCODING is NULL, then *STR is presumed to be encoded in
 * UTF-8.
 *
 * *WAS_NORMALIZED is set to TRUE when *STR needed line ending normalization.
 * Otherwise it is set to FALSE.
 *
 * SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
normalize_string(const svn_string_t **str,
                 svn_boolean_t *was_normalized,
                 const char *source_prop_encoding,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_string_t *new_str;

  *was_normalized = FALSE;

  if (*str == NULL)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT((*str)->data != NULL);

  if (source_prop_encoding == NULL)
    source_prop_encoding = "UTF-8";

  new_str = NULL;
  SVN_ERR(svn_subst_translate_string2(&new_str, NULL, was_normalized,
                                      *str, source_prop_encoding, TRUE,
                                      result_pool, scratch_pool));
  *str = new_str;

  return SVN_NO_ERROR;
}

/* Remove r0 references from the mergeinfo string *STR.
 *
 * r0 was never a valid mergeinfo reference and cannot be committed with
 * recent servers, but can be committed through a server older than 1.6.18
 * for HTTP or older than 1.6.17 for the other protocols. See issue #4476
 * "Mergeinfo containing r0 makes svnsync and dump and load fail".
 *
 * Set *WAS_CHANGED to TRUE if *STR was changed, otherwise to FALSE.
 */
static svn_error_t *
remove_r0_mergeinfo(const svn_string_t **str,
                    svn_boolean_t *was_changed,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *new_str = svn_stringbuf_create_empty(result_pool);
  apr_array_header_t *lines;
  int i;

  SVN_ERR_ASSERT(*str && (*str)->data);

  *was_changed = FALSE;

  /* for each line */
  lines = svn_cstring_split((*str)->data, "\n", FALSE, scratch_pool);

  for (i = 0; i < lines->nelts; i++)
    {
      char *line = APR_ARRAY_IDX(lines, i, char *);
      char *colon;
      char *rangelist;

      /* split at the last colon */
      colon = strrchr(line, ':');

      if (! colon)
        return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                 _("Missing colon in svn:mergeinfo "
                                   "property"));

      rangelist = colon + 1;

      /* remove r0 */
      if (colon[1] == '0')
        {
          if (strncmp(rangelist, "0*,", 3) == 0)
            {
              rangelist += 3;
            }
          else if (strcmp(rangelist, "0*") == 0
                   || strncmp(rangelist, "0,", 2) == 0
                   || strncmp(rangelist, "0-1*", 4) == 0
                   || strncmp(rangelist, "0-1,", 4) == 0
                   || strcmp(rangelist, "0-1") == 0)
            {
              rangelist += 2;
            }
          else if (strcmp(rangelist, "0") == 0)
            {
              rangelist += 1;
            }
          else if (strncmp(rangelist, "0-", 2) == 0)
            {
              rangelist[0] = '1';
            }
        }

      /* reassemble */
      if (rangelist[0])
        {
          if (new_str->len)
            svn_stringbuf_appendbyte(new_str, '\n');
          svn_stringbuf_appendbytes(new_str, line, colon + 1 - line);
          svn_stringbuf_appendcstr(new_str, rangelist);
        }
    }

  if (strcmp((*str)->data, new_str->data) != 0)
    {
      *was_changed = TRUE;
    }

  *str = svn_stringbuf__morph_into_string(new_str);
  return SVN_NO_ERROR;
}


/* Normalize the encoding and line ending style of the values of properties
 * in REV_PROPS that "need translation" (according to
 * svn_prop_needs_translation(), which is currently all svn:* props) so that
 * they are encoded in UTF-8 and contain only LF (\n) line endings.
 *
 * The number of properties that needed line ending normalization is returned in
 * *NORMALIZED_COUNT.
 *
 * No re-encoding is performed if SOURCE_PROP_ENCODING is NULL.
 */
svn_error_t *
svnsync_normalize_revprops(apr_hash_t *rev_props,
                           int *normalized_count,
                           const char *source_prop_encoding,
                           apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  *normalized_count = 0;

  for (hi = apr_hash_first(pool, rev_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);
      const svn_string_t *propval = apr_hash_this_val(hi);

      if (svn_prop_needs_translation(propname))
        {
          svn_boolean_t was_normalized;
          SVN_ERR(normalize_string(&propval, &was_normalized,
                  source_prop_encoding, pool, pool));

          /* Replace the existing prop value. */
          svn_hash_sets(rev_props, propname, propval);

          if (was_normalized)
            (*normalized_count)++; /* Count it. */
        }
    }
  return SVN_NO_ERROR;
}


/*** Synchronization Editor ***/

/* This editor has a couple of jobs.
 *
 * First, it needs to filter out the propchanges that can't be passed over
 * libsvn_ra.
 *
 * Second, it needs to adjust for the fact that we might not actually have
 * permission to see all of the data from the remote repository, which means
 * we could get revisions that are totally empty from our point of view.
 *
 * Third, it needs to adjust copyfrom paths, adding the root url for the
 * destination repository to the beginning of them.
 */


/* Edit baton */
typedef struct edit_baton_t {
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  const char *to_url;  /* URL we're copying into, for correct copyfrom URLs */
  const char *source_prop_encoding;
  svn_boolean_t called_open_root;
  svn_boolean_t got_textdeltas;
  svn_revnum_t base_revision;
  svn_boolean_t quiet;
  svn_boolean_t mergeinfo_tweaked;  /* Did we tweak svn:mergeinfo? */
  svn_boolean_t strip_mergeinfo;    /* Are we stripping svn:mergeinfo? */
  svn_boolean_t migrate_svnmerge;   /* Are we converting svnmerge.py data? */
  svn_boolean_t mergeinfo_stripped; /* Did we strip svn:mergeinfo? */
  svn_boolean_t svnmerge_migrated;  /* Did we convert svnmerge.py data? */
  svn_boolean_t svnmerge_blocked;   /* Was there any blocked svnmerge data? */
  int *normalized_node_props_counter;  /* Where to count normalizations? */
} edit_baton_t;


/* A dual-purpose baton for files and directories. */
typedef struct node_baton_t {
  void *edit_baton;
  void *wrapped_node_baton;
} node_baton_t;


/*** Editor vtable functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                 target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  edit_baton_t *eb = edit_baton;
  node_baton_t *dir_baton = apr_palloc(pool, sizeof(*dir_baton));

  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                        base_revision, pool,
                                        &dir_baton->wrapped_node_baton));

  eb->called_open_root = TRUE;
  dir_baton->edit_baton = edit_baton;
  *root_baton = dir_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;

  return eb->wrapped_editor->delete_entry(path, base_revision,
                                          pb->wrapped_node_baton, pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *b = apr_palloc(pool, sizeof(*b));

  /* if copyfrom_path is an fspath create a proper uri */
  if (copyfrom_path && copyfrom_path[0] == '/')
    copyfrom_path = svn_path_url_add_component2(eb->to_url,
                                                copyfrom_path + 1, pool);

  SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                            copyfrom_path,
                                            copyfrom_rev, pool,
                                            &b->wrapped_node_baton));

  b->edit_baton = eb;
  *child_baton = b;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *db = apr_palloc(pool, sizeof(*db));

  SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_node_baton,
                                             base_revision, pool,
                                             &db->wrapped_node_baton));

  db->edit_baton = eb;
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  /* if copyfrom_path is an fspath create a proper uri */
  if (copyfrom_path && copyfrom_path[0] == '/')
    copyfrom_path = svn_path_url_add_component2(eb->to_url,
                                                copyfrom_path + 1, pool);

  SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_node_baton,
                                       copyfrom_path, copyfrom_rev,
                                       pool, &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_node_baton,
                                        base_revision, pool,
                                        &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  if (! eb->quiet)
    {
      if (! eb->got_textdeltas)
        SVN_ERR(svn_cmdline_printf(pool, _("Transmitting file data ")));
      SVN_ERR(svn_cmdline_printf(pool, "."));
      SVN_ERR(svn_cmdline_fflush(stdout));
    }

  eb->got_textdeltas = TRUE;
  return eb->wrapped_editor->apply_textdelta(fb->wrapped_node_baton,
                                             base_checksum, pool,
                                             handler, handler_baton);
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  return eb->wrapped_editor->close_file(fb->wrapped_node_baton,
                                        text_checksum, pool);
}

static svn_error_t *
absent_file(const char *path,
            void *file_baton,
            apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  return eb->wrapped_editor->absent_file(path, fb->wrapped_node_baton, pool);
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->close_directory(db->wrapped_node_baton, pool);
}

static svn_error_t *
absent_directory(const char *path,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->absent_directory(path, db->wrapped_node_baton,
                                              pool);
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  /* only regular properties can pass over libsvn_ra */
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  /* Maybe drop svn:mergeinfo.  */
  if (eb->strip_mergeinfo && (strcmp(name, SVN_PROP_MERGEINFO) == 0))
    {
      eb->mergeinfo_stripped = TRUE;
      return SVN_NO_ERROR;
    }

  /* Maybe drop (errantly set, as this is a file) svnmerge.py properties. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-integrated") == 0))
    {
      eb->svnmerge_migrated = TRUE;
      return SVN_NO_ERROR;
    }

  /* Remember if we see any svnmerge-blocked properties.  (They really
     shouldn't be here, as this is a file, but whatever...)  */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-blocked") == 0))
    {
      eb->svnmerge_blocked = TRUE;
    }

  /* Normalize svn:* properties as necessary. */
  if (svn_prop_needs_translation(name))
    {
      svn_boolean_t was_normalized;
      svn_boolean_t mergeinfo_tweaked = FALSE;

      /* Normalize encoding to UTF-8, and EOL style to LF. */
      SVN_ERR(normalize_string(&value, &was_normalized,
                               eb->source_prop_encoding, pool, pool));
      /* Correct malformed mergeinfo. */
      if (value && strcmp(name, SVN_PROP_MERGEINFO) == 0)
        {
          SVN_ERR(remove_r0_mergeinfo(&value, &mergeinfo_tweaked,
                                      pool, pool));
          if (mergeinfo_tweaked)
            eb->mergeinfo_tweaked = TRUE;
        }
      if (was_normalized)
        (*(eb->normalized_node_props_counter))++;
    }

  return eb->wrapped_editor->change_file_prop(fb->wrapped_node_baton,
                                              name, value, pool);
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;

  /* Only regular properties can pass over libsvn_ra */
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  /* Maybe drop svn:mergeinfo.  */
  if (eb->strip_mergeinfo && (strcmp(name, SVN_PROP_MERGEINFO) == 0))
    {
      eb->mergeinfo_stripped = TRUE;
      return SVN_NO_ERROR;
    }

  /* Maybe convert svnmerge-integrated data into svn:mergeinfo.  (We
     ignore svnmerge-blocked for now.) */
  /* ### FIXME: Consult the mirror repository's HEAD prop values and
     ### merge svn:mergeinfo, svnmerge-integrated, and svnmerge-blocked. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-integrated") == 0))
    {
      if (value)
        {
          /* svnmerge-integrated differs from svn:mergeinfo in a pair
             of ways.  First, it can use tabs, newlines, or spaces to
             delimit source information.  Secondly, the source paths
             are relative URLs, whereas svn:mergeinfo uses relative
             paths (not URI-encoded). */
          svn_error_t *err;
          svn_stringbuf_t *mergeinfo_buf = svn_stringbuf_create_empty(pool);
          svn_mergeinfo_t mergeinfo;
          int i;
          apr_array_header_t *sources =
            svn_cstring_split(value->data, " \t\n", TRUE, pool);
          svn_string_t *new_value;

          for (i = 0; i < sources->nelts; i++)
            {
              const char *rel_path;
              apr_array_header_t *path_revs =
                svn_cstring_split(APR_ARRAY_IDX(sources, i, const char *),
                                  ":", TRUE, pool);

              /* ### TODO: Warn? */
              if (path_revs->nelts != 2)
                continue;

              /* Append this source's mergeinfo data. */
              rel_path = APR_ARRAY_IDX(path_revs, 0, const char *);
              rel_path = svn_path_uri_decode(rel_path, pool);
              svn_stringbuf_appendcstr(mergeinfo_buf, rel_path);
              svn_stringbuf_appendcstr(mergeinfo_buf, ":");
              svn_stringbuf_appendcstr(mergeinfo_buf,
                                       APR_ARRAY_IDX(path_revs, 1,
                                                     const char *));
              svn_stringbuf_appendcstr(mergeinfo_buf, "\n");
            }

          /* Try to parse the mergeinfo string we've created, just to
             check for bogosity.  If all goes well, we'll unparse it
             again and use that as our property value.  */
          err = svn_mergeinfo_parse(&mergeinfo, mergeinfo_buf->data, pool);
          if (err)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          SVN_ERR(svn_mergeinfo_to_string(&new_value, mergeinfo, pool));
          value = new_value;
        }
      name = SVN_PROP_MERGEINFO;
      eb->svnmerge_migrated = TRUE;
    }

  /* Remember if we see any svnmerge-blocked properties. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-blocked") == 0))
    {
      eb->svnmerge_blocked = TRUE;
    }

  /* Normalize svn:* properties as necessary. */
  if (svn_prop_needs_translation(name))
    {
      svn_boolean_t was_normalized;
      svn_boolean_t mergeinfo_tweaked = FALSE;

      /* Normalize encoding to UTF-8, and EOL style to LF. */
      SVN_ERR(normalize_string(&value, &was_normalized, eb->source_prop_encoding,
                               pool, pool));
      /* Maybe adjust svn:mergeinfo. */
      if (value && strcmp(name, SVN_PROP_MERGEINFO) == 0)
        {
          SVN_ERR(remove_r0_mergeinfo(&value, &mergeinfo_tweaked,
                                      pool, pool));
          if (mergeinfo_tweaked)
            eb->mergeinfo_tweaked = TRUE;
        }
      if (was_normalized)
        (*(eb->normalized_node_props_counter))++;
    }

  return eb->wrapped_editor->change_dir_prop(db->wrapped_node_baton,
                                             name, value, pool);
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;

  /* If we haven't opened the root yet, that means we're transfering
     an empty revision, probably because we aren't allowed to see the
     contents for some reason.  In any event, we need to open the root
     and close it again, before we can close out the edit, or the
     commit will fail. */

  if (! eb->called_open_root)
    {
      void *baton;
      SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                            eb->base_revision, pool,
                                            &baton));
      SVN_ERR(eb->wrapped_editor->close_directory(baton, pool));
    }

  if (! eb->quiet)
    {
      if (eb->got_textdeltas)
        SVN_ERR(svn_cmdline_printf(pool, "\n"));
      if (eb->mergeinfo_tweaked)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Adjusted Subversion mergeinfo in "
                                   "this revision.\n"));
      if (eb->mergeinfo_stripped)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Dropped Subversion mergeinfo "
                                   "from this revision.\n"));
      if (eb->svnmerge_migrated)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Migrated 'svnmerge-integrated' in "
                                   "this revision.\n"));
      if (eb->svnmerge_blocked)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Saw 'svnmerge-blocked' in this "
                                   "revision (but didn't migrate it).\n"));
    }

  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->abort_edit(eb->wrapped_edit_baton, pool);
}


/*** Editor factory function ***/

svn_error_t *
svnsync_get_sync_editor(const svn_delta_editor_t *wrapped_editor,
                        void *wrapped_edit_baton,
                        svn_revnum_t base_revision,
                        const char *to_url,
                        const char *source_prop_encoding,
                        svn_boolean_t quiet,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        int *normalized_node_props_counter,
                        apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  edit_baton_t *eb = apr_pcalloc(pool, sizeof(*eb));

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;
  tree_editor->abort_edit = abort_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->base_revision = base_revision;
  eb->to_url = to_url;
  eb->source_prop_encoding = source_prop_encoding;
  eb->quiet = quiet;
  eb->normalized_node_props_counter = normalized_node_props_counter;

  if (getenv("SVNSYNC_UNSUPPORTED_STRIP_MERGEINFO"))
    {
      eb->strip_mergeinfo = TRUE;
    }
  if (getenv("SVNSYNC_UNSUPPORTED_MIGRATE_SVNMERGE"))
    {
      /* Current we can't merge property values.  That's only possible
         if all the properties to be merged were always modified in
         exactly the same revisions, or if we allow ourselves to
         lookup the current state of properties in the sync
         destination.  So for now, migrating svnmerge.py data implies
         stripping pre-existing svn:mergeinfo. */
      /* ### FIXME: Do a real migration by consulting the mirror
         ### repository's HEAD propvalues and merging svn:mergeinfo,
         ### svnmerge-integrated, and svnmerge-blocked together. */
      eb->migrate_svnmerge = TRUE;
      eb->strip_mergeinfo = TRUE;
    }

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}

