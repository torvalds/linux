/*
 * diff_tree.c :  default diff tree processor
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

#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>

#include <assert.h>

#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"
#include "svn_private_config.h"

typedef struct tree_processor_t
{
  svn_diff_tree_processor_t tp;

  /* void *future_extension */
} tree_processor_t;


static svn_error_t *
default_dir_opened(void **new_dir_baton,
                   svn_boolean_t *skip,
                   svn_boolean_t *skip_children,
                   const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   const svn_diff_source_t *copyfrom_source,
                   void *parent_dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  *new_dir_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath, NULL, right_source,
                                dir_baton, processor,
                                scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    /*const*/ apr_hash_t *left_props,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath, left_source, NULL,
                                dir_baton, processor,
                                scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->dir_closed(relpath,
                                left_source, right_source,
                                dir_baton,
                                processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_dir_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_opened(void **new_file_baton,
                    svn_boolean_t *skip,
                    const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    const svn_diff_source_t *copyfrom_source,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  *new_file_baton = dir_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_added(const char *relpath,
                   const svn_diff_source_t *copyfrom_source,
                   const svn_diff_source_t *right_source,
                   const char *copyfrom_file,
                   const char *right_file,
                   /*const*/ apr_hash_t *copyfrom_props,
                   /*const*/ apr_hash_t *right_props,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 NULL, right_source,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_deleted(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const char *left_file,
                     /*const*/ apr_hash_t *left_props,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 left_source, NULL,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_changed(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const svn_diff_source_t *right_source,
                     const char *left_file,
                     const char *right_file,
                     /*const*/ apr_hash_t *left_props,
                     /*const*/ apr_hash_t *right_props,
                     svn_boolean_t file_modified,
                     const apr_array_header_t *prop_changes,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(processor->file_closed(relpath,
                                 left_source, right_source,
                                 file_baton, processor, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
default_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
default_node_absent(const char *relpath,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

svn_diff_tree_processor_t *
svn_diff__tree_processor_create(void *baton,
                                apr_pool_t *result_pool)
{
  tree_processor_t *wrapper;
  wrapper = apr_pcalloc(result_pool, sizeof(*wrapper));

  wrapper->tp.baton        = baton;

  wrapper->tp.dir_opened   = default_dir_opened;
  wrapper->tp.dir_added    = default_dir_added;
  wrapper->tp.dir_deleted  = default_dir_deleted;
  wrapper->tp.dir_changed  = default_dir_changed;
  wrapper->tp.dir_closed   = default_dir_closed;

  wrapper->tp.file_opened   = default_file_opened;
  wrapper->tp.file_added    = default_file_added;
  wrapper->tp.file_deleted  = default_file_deleted;
  wrapper->tp.file_changed  = default_file_changed;
  wrapper->tp.file_closed   = default_file_closed;

  wrapper->tp.node_absent   = default_node_absent;


  return &wrapper->tp;
}

struct reverse_tree_baton_t
{
  const svn_diff_tree_processor_t *processor;
  const char *prefix_relpath;
};

static svn_error_t *
reverse_dir_opened(void **new_dir_baton,
                   svn_boolean_t *skip,
                   svn_boolean_t *skip_children,
                   const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   const svn_diff_source_t *copyfrom_source,
                   void *parent_dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    right_source, left_source,
                                    NULL /* copyfrom */,
                                    parent_dir_baton,
                                    rb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_deleted(relpath,
                                     right_source,
                                     right_props,
                                     dir_baton,
                                     rb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    /*const*/ apr_hash_t *left_props,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_added(relpath,
                                   NULL,
                                   left_source,
                                   NULL,
                                   left_props,
                                   dir_baton,
                                   rb->processor,
                                   scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;
  apr_array_header_t *reversed_prop_changes = NULL;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  if (prop_changes)
    {
      SVN_ERR_ASSERT(left_props != NULL && right_props != NULL);
      SVN_ERR(svn_prop_diffs(&reversed_prop_changes, left_props, right_props,
                             scratch_pool));
    }

  SVN_ERR(rb->processor->dir_changed(relpath,
                                     right_source,
                                     left_source,
                                     right_props,
                                     left_props,
                                     reversed_prop_changes,
                                     dir_baton,
                                     rb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_dir_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->dir_closed(relpath,
                                    right_source,
                                    left_source,
                                    dir_baton,
                                    rb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_opened(void **new_file_baton,
                    svn_boolean_t *skip,
                    const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    const svn_diff_source_t *copyfrom_source,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     right_source,
                                     left_source,
                                     NULL /* copy_from */,
                                     dir_baton,
                                     rb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_added(const char *relpath,
                   const svn_diff_source_t *copyfrom_source,
                   const svn_diff_source_t *right_source,
                   const char *copyfrom_file,
                   const char *right_file,
                   /*const*/ apr_hash_t *copyfrom_props,
                   /*const*/ apr_hash_t *right_props,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_deleted(relpath,
                                      right_source,
                                      right_file,
                                      right_props,
                                      file_baton,
                                      rb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_deleted(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const char *left_file,
                     /*const*/ apr_hash_t *left_props,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_added(relpath,
                                    NULL /* copyfrom src */,
                                    left_source,
                                    NULL /* copyfrom file */,
                                    left_file,
                                    NULL /* copyfrom props */,
                                    left_props,
                                    file_baton,
                                    rb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_changed(const char *relpath,
                     const svn_diff_source_t *left_source,
                     const svn_diff_source_t *right_source,
                     const char *left_file,
                     const char *right_file,
                     /*const*/ apr_hash_t *left_props,
                     /*const*/ apr_hash_t *right_props,
                     svn_boolean_t file_modified,
                     const apr_array_header_t *prop_changes,
                     void *file_baton,
                     const svn_diff_tree_processor_t *processor,
                     apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;
  apr_array_header_t *reversed_prop_changes = NULL;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  if (prop_changes)
    {
      SVN_ERR_ASSERT(left_props != NULL && right_props != NULL);
      SVN_ERR(svn_prop_diffs(&reversed_prop_changes, left_props, right_props,
                             scratch_pool));
    }

  SVN_ERR(rb->processor->file_changed(relpath,
                                      right_source,
                                      left_source,
                                      right_file,
                                      left_file,
                                      right_props,
                                      left_props,
                                      file_modified,
                                      reversed_prop_changes,
                                      file_baton,
                                      rb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->file_closed(relpath,
                                     right_source,
                                     left_source,
                                     file_baton,
                                     rb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
reverse_node_absent(const char *relpath,
                    void *dir_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct reverse_tree_baton_t *rb = processor->baton;

  if (rb->prefix_relpath)
    relpath = svn_relpath_join(rb->prefix_relpath, relpath, scratch_pool);

  SVN_ERR(rb->processor->node_absent(relpath,
                                    dir_baton,
                                    rb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_reverse_create(const svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool)
{
  struct reverse_tree_baton_t *rb;
  svn_diff_tree_processor_t *reverse;

  rb = apr_pcalloc(result_pool, sizeof(*rb));
  rb->processor = processor;
  if (prefix_relpath)
    rb->prefix_relpath = apr_pstrdup(result_pool, prefix_relpath);

  reverse = svn_diff__tree_processor_create(rb, result_pool);

  reverse->dir_opened   = reverse_dir_opened;
  reverse->dir_added    = reverse_dir_added;
  reverse->dir_deleted  = reverse_dir_deleted;
  reverse->dir_changed  = reverse_dir_changed;
  reverse->dir_closed   = reverse_dir_closed;

  reverse->file_opened   = reverse_file_opened;
  reverse->file_added    = reverse_file_added;
  reverse->file_deleted  = reverse_file_deleted;
  reverse->file_changed  = reverse_file_changed;
  reverse->file_closed   = reverse_file_closed;

  reverse->node_absent   = reverse_node_absent;

  return reverse;
}

struct filter_tree_baton_t
{
  const svn_diff_tree_processor_t *processor;
  const char *prefix_relpath;
};

static svn_error_t *
filter_dir_opened(void **new_dir_baton,
                  svn_boolean_t *skip,
                  svn_boolean_t *skip_children,
                  const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const svn_diff_source_t *copyfrom_source,
                  void *parent_dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);

  if (! relpath)
    {
      /* Skip work for this, but NOT for DESCENDANTS */
      *skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(fb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    left_source, right_source,
                                    copyfrom_source,
                                    parent_dir_baton,
                                    fb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_added(const char *relpath,
                 const svn_diff_source_t *copyfrom_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *copyfrom_props,
                 /*const*/ apr_hash_t *right_props,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_added(relpath,
                                   copyfrom_source,
                                   right_source,
                                   copyfrom_props,
                                   right_props,
                                   dir_baton,
                                   fb->processor,
                                   scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_deleted(const char *relpath,
                   const svn_diff_source_t *left_source,
                   /*const*/ apr_hash_t *left_props,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_deleted(relpath,
                                     left_source,
                                     left_props,
                                     dir_baton,
                                     fb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_changed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   /*const*/ apr_hash_t *left_props,
                   /*const*/ apr_hash_t *right_props,
                   const apr_array_header_t *prop_changes,
                   void *dir_baton,
                   const struct svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_changed(relpath,
                                     left_source,
                                     right_source,
                                     left_props,
                                     right_props,
                                     prop_changes,
                                     dir_baton,
                                     fb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_closed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_opened(void **new_file_baton,
                   svn_boolean_t *skip,
                   const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   const svn_diff_source_t *copyfrom_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);

  if (! relpath)
    {
      *skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(fb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     left_source,
                                     right_source,
                                     copyfrom_source,
                                     dir_baton,
                                     fb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  const char *copyfrom_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *file_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_added(relpath,
                                    copyfrom_source,
                                    right_source,
                                    copyfrom_file,
                                    right_file,
                                    copyfrom_props,
                                    right_props,
                                    file_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const char *left_file,
                    /*const*/ apr_hash_t *left_props,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_deleted(relpath,
                                      left_source,
                                      left_file,
                                      left_props,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    const char *left_file,
                    const char *right_file,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    svn_boolean_t file_modified,
                    const apr_array_header_t *prop_changes,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_changed(relpath,
                                      left_source,
                                      right_source,
                                      left_file,
                                      right_file,
                                      left_props,
                                      right_props,
                                      file_modified,
                                      prop_changes,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->file_closed(relpath,
                                     left_source,
                                     right_source,
                                     file_baton,
                                     fb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_node_absent(const char *relpath,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  relpath = svn_relpath_skip_ancestor(fb->prefix_relpath, relpath);
  assert(relpath != NULL); /* Driver error */

  SVN_ERR(fb->processor->node_absent(relpath,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_filter_create(const svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool)
{
  struct filter_tree_baton_t *fb;
  svn_diff_tree_processor_t *filter;

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->processor = processor;
  if (prefix_relpath)
    fb->prefix_relpath = apr_pstrdup(result_pool, prefix_relpath);

  filter = svn_diff__tree_processor_create(fb, result_pool);

  filter->dir_opened   = filter_dir_opened;
  filter->dir_added    = filter_dir_added;
  filter->dir_deleted  = filter_dir_deleted;
  filter->dir_changed  = filter_dir_changed;
  filter->dir_closed   = filter_dir_closed;

  filter->file_opened   = filter_file_opened;
  filter->file_added    = filter_file_added;
  filter->file_deleted  = filter_file_deleted;
  filter->file_changed  = filter_file_changed;
  filter->file_closed   = filter_file_closed;

  filter->node_absent   = filter_node_absent;

  return filter;
}

struct copy_as_changed_baton_t
{
  const svn_diff_tree_processor_t *processor;
};

static svn_error_t *
copy_as_changed_dir_opened(void **new_dir_baton,
                           svn_boolean_t *skip,
                           svn_boolean_t *skip_children,
                           const char *relpath,
                           const svn_diff_source_t *left_source,
                           const svn_diff_source_t *right_source,
                           const svn_diff_source_t *copyfrom_source,
                           void *parent_dir_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (!left_source && copyfrom_source)
    {
      assert(right_source != NULL);

      left_source = copyfrom_source;
      copyfrom_source = NULL;
    }

  SVN_ERR(cb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    left_source, right_source,
                                    copyfrom_source,
                                    parent_dir_baton,
                                    cb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_added(const char *relpath,
                          const svn_diff_source_t *copyfrom_source,
                          const svn_diff_source_t *right_source,
                          /*const*/ apr_hash_t *copyfrom_props,
                          /*const*/ apr_hash_t *right_props,
                          void *dir_baton,
                          const svn_diff_tree_processor_t *processor,
                          apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (copyfrom_source)
    {
      apr_array_header_t *propchanges;
      SVN_ERR(svn_prop_diffs(&propchanges, right_props, copyfrom_props,
                             scratch_pool));
      SVN_ERR(cb->processor->dir_changed(relpath,
                                         copyfrom_source,
                                         right_source,
                                         copyfrom_props,
                                         right_props,
                                         propchanges,
                                         dir_baton,
                                         cb->processor,
                                         scratch_pool));
    }
  else
    {
      SVN_ERR(cb->processor->dir_added(relpath,
                                       copyfrom_source,
                                       right_source,
                                       copyfrom_props,
                                       right_props,
                                       dir_baton,
                                       cb->processor,
                                       scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_deleted(const char *relpath,
                            const svn_diff_source_t *left_source,
                            /*const*/ apr_hash_t *left_props,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_deleted(relpath,
                                     left_source,
                                     left_props,
                                     dir_baton,
                                     cb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_changed(const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            /*const*/ apr_hash_t *left_props,
                            /*const*/ apr_hash_t *right_props,
                            const apr_array_header_t *prop_changes,
                            void *dir_baton,
                            const struct svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_changed(relpath,
                                     left_source,
                                     right_source,
                                     left_props,
                                     right_props,
                                     prop_changes,
                                     dir_baton,
                                     cb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_dir_closed(const char *relpath,
                           const svn_diff_source_t *left_source,
                           const svn_diff_source_t *right_source,
                           void *dir_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    cb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_opened(void **new_file_baton,
                            svn_boolean_t *skip,
                            const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            const svn_diff_source_t *copyfrom_source,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (!left_source && copyfrom_source)
    {
      assert(right_source != NULL);

      left_source = copyfrom_source;
      copyfrom_source = NULL;
    }

  SVN_ERR(cb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     left_source,
                                     right_source,
                                     copyfrom_source,
                                     dir_baton,
                                     cb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_added(const char *relpath,
                           const svn_diff_source_t *copyfrom_source,
                           const svn_diff_source_t *right_source,
                           const char *copyfrom_file,
                           const char *right_file,
                           /*const*/ apr_hash_t *copyfrom_props,
                           /*const*/ apr_hash_t *right_props,
                           void *file_baton,
                           const svn_diff_tree_processor_t *processor,
                           apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  if (copyfrom_source)
    {
      apr_array_header_t *propchanges;
      svn_boolean_t same;
      SVN_ERR(svn_prop_diffs(&propchanges, right_props, copyfrom_props,
                             scratch_pool));

      /* "" is sometimes a marker for just modified (E.g. no-textdeltas),
         and it is certainly not a file */
      if (*copyfrom_file && *right_file)
        {
          SVN_ERR(svn_io_files_contents_same_p(&same, copyfrom_file,
                                               right_file, scratch_pool));
        }
      else
        same = FALSE;

      SVN_ERR(cb->processor->file_changed(relpath,
                                          copyfrom_source,
                                          right_source,
                                          copyfrom_file,
                                          right_file,
                                          copyfrom_props,
                                          right_props,
                                          !same,
                                          propchanges,
                                          file_baton,
                                          cb->processor,
                                          scratch_pool));
    }
  else
    {
      SVN_ERR(cb->processor->file_added(relpath,
                                        copyfrom_source,
                                        right_source,
                                        copyfrom_file,
                                        right_file,
                                        copyfrom_props,
                                        right_props,
                                        file_baton,
                                        cb->processor,
                                        scratch_pool));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_deleted(const char *relpath,
                             const svn_diff_source_t *left_source,
                             const char *left_file,
                             /*const*/ apr_hash_t *left_props,
                             void *file_baton,
                             const svn_diff_tree_processor_t *processor,
                             apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_deleted(relpath,
                                      left_source,
                                      left_file,
                                      left_props,
                                      file_baton,
                                      cb->processor,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_changed(const char *relpath,
                             const svn_diff_source_t *left_source,
                             const svn_diff_source_t *right_source,
                             const char *left_file,
                             const char *right_file,
                             /*const*/ apr_hash_t *left_props,
                             /*const*/ apr_hash_t *right_props,
                             svn_boolean_t file_modified,
                             const apr_array_header_t *prop_changes,
                             void *file_baton,
                             const svn_diff_tree_processor_t *processor,
                             apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_changed(relpath,
                                      left_source,
                                      right_source,
                                      left_file,
                                      right_file,
                                      left_props,
                                      right_props,
                                      file_modified,
                                      prop_changes,
                                      file_baton,
                                      cb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_file_closed(const char *relpath,
                            const svn_diff_source_t *left_source,
                            const svn_diff_source_t *right_source,
                            void *file_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->file_closed(relpath,
                                     left_source,
                                     right_source,
                                     file_baton,
                                     cb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_as_changed_node_absent(const char *relpath,
                            void *dir_baton,
                            const svn_diff_tree_processor_t *processor,
                            apr_pool_t *scratch_pool)
{
  struct copy_as_changed_baton_t *cb = processor->baton;

  SVN_ERR(cb->processor->node_absent(relpath,
                                    dir_baton,
                                    cb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}


const svn_diff_tree_processor_t *
svn_diff__tree_processor_copy_as_changed_create(
                        const svn_diff_tree_processor_t * processor,
                        apr_pool_t *result_pool)
{
  struct copy_as_changed_baton_t *cb;
  svn_diff_tree_processor_t *filter;

  cb = apr_pcalloc(result_pool, sizeof(*cb));
  cb->processor = processor;

  filter = svn_diff__tree_processor_create(cb, result_pool);
  filter->dir_opened   = copy_as_changed_dir_opened;
  filter->dir_added    = copy_as_changed_dir_added;
  filter->dir_deleted  = copy_as_changed_dir_deleted;
  filter->dir_changed  = copy_as_changed_dir_changed;
  filter->dir_closed   = copy_as_changed_dir_closed;

  filter->file_opened   = copy_as_changed_file_opened;
  filter->file_added    = copy_as_changed_file_added;
  filter->file_deleted  = copy_as_changed_file_deleted;
  filter->file_changed  = copy_as_changed_file_changed;
  filter->file_closed   = copy_as_changed_file_closed;

  filter->node_absent   = copy_as_changed_node_absent;

  return filter;
}


/* Processor baton for the tee tree processor */
struct tee_baton_t
{
  const svn_diff_tree_processor_t *p1;
  const svn_diff_tree_processor_t *p2;
};

/* Wrapper baton for file and directory batons in the tee processor */
struct tee_node_baton_t
{
  void *baton1;
  void *baton2;
};

static svn_error_t *
tee_dir_opened(void **new_dir_baton,
               svn_boolean_t *skip,
               svn_boolean_t *skip_children,
               const char *relpath,
               const svn_diff_source_t *left_source,
               const svn_diff_source_t *right_source,
               const svn_diff_source_t *copyfrom_source,
               void *parent_dir_baton,
               const svn_diff_tree_processor_t *processor,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *pb = parent_dir_baton;
  struct tee_node_baton_t *nb = apr_pcalloc(result_pool, sizeof(*nb));

  SVN_ERR(tb->p1->dir_opened(&(nb->baton1),
                             skip,
                             skip_children,
                             relpath,
                             left_source,
                             right_source,
                             copyfrom_source,
                             pb ? pb->baton1 : NULL,
                             tb->p1,
                             result_pool,
                             scratch_pool));

  SVN_ERR(tb->p2->dir_opened(&(nb->baton2),
                             skip,
                             skip_children,
                             relpath,
                             left_source,
                             right_source,
                             copyfrom_source,
                             pb ? pb->baton2 : NULL,
                             tb->p2,
                             result_pool,
                             scratch_pool));

  *new_dir_baton = nb;

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_added(const char *relpath,
              const svn_diff_source_t *copyfrom_source,
              const svn_diff_source_t *right_source,
              /*const*/ apr_hash_t *copyfrom_props,
              /*const*/ apr_hash_t *right_props,
              void *dir_baton,
              const svn_diff_tree_processor_t *processor,
              apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_added(relpath,
                            copyfrom_source,
                            right_source,
                            copyfrom_props,
                            right_props,
                            db->baton1,
                            tb->p1,
                            scratch_pool));

  SVN_ERR(tb->p2->dir_added(relpath,
                            copyfrom_source,
                            right_source,
                            copyfrom_props,
                            right_props,
                            db->baton2,
                            tb->p2,
                            scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_deleted(const char *relpath,
                const svn_diff_source_t *left_source,
                /*const*/ apr_hash_t *left_props,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_deleted(relpath,
                              left_source,
                              left_props,
                              db->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->dir_deleted(relpath,
                              left_source,
                              left_props,
                              db->baton2,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    const apr_array_header_t *prop_changes,
                    void *dir_baton,
                    const struct svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_changed(relpath,
                              left_source,
                              right_source,
                              left_props,
                              right_props,
                              prop_changes,
                              db->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->dir_changed(relpath,
                              left_source,
                              right_source,
                              left_props,
                              right_props,
                              prop_changes,
                              db->baton2,
                              tb->p2,
                              scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_dir_closed(const char *relpath,
               const svn_diff_source_t *left_source,
               const svn_diff_source_t *right_source,
               void *dir_baton,
               const svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->dir_closed(relpath,
                             left_source,
                             right_source,
                             db->baton1,
                             tb->p1,
                             scratch_pool));

  SVN_ERR(tb->p2->dir_closed(relpath,
                             left_source,
                             right_source,
                             db->baton2,
                             tb->p2,
                             scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_opened(void **new_file_baton,
                svn_boolean_t *skip,
                const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                const svn_diff_source_t *copyfrom_source,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *pb = dir_baton;
  struct tee_node_baton_t *nb = apr_pcalloc(result_pool, sizeof(*nb));

  SVN_ERR(tb->p1->file_opened(&(nb->baton1),
                              skip,
                              relpath,
                              left_source,
                              right_source,
                              copyfrom_source,
                              pb ? pb->baton1 : NULL,
                              tb->p1,
                              result_pool,
                              scratch_pool));

  SVN_ERR(tb->p2->file_opened(&(nb->baton2),
                              skip,
                              relpath,
                              left_source,
                              right_source,
                              copyfrom_source,
                              pb ? pb->baton2 : NULL,
                              tb->p2,
                              result_pool,
                              scratch_pool));

  *new_file_baton = nb;

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               const char *copyfrom_file,
               const char *right_file,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *file_baton,
               const svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_added(relpath,
                             copyfrom_source,
                             right_source,
                             copyfrom_file,
                             right_file,
                             copyfrom_props,
                             right_props,
                             fb->baton1,
                             tb->p1,
                             scratch_pool));

  SVN_ERR(tb->p2->file_added(relpath,
                             copyfrom_source,
                             right_source,
                             copyfrom_file,
                             right_file,
                             copyfrom_props,
                             right_props,
                             fb->baton2,
                             tb->p2,
                             scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const char *left_file,
                 /*const*/ apr_hash_t *left_props,
                 void *file_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_deleted(relpath,
                               left_source,
                               left_file,
                               left_props,
                               fb->baton1,
                               tb->p1,
                               scratch_pool));

  SVN_ERR(tb->p2->file_deleted(relpath,
                               left_source,
                               left_file,
                               left_props,
                               fb->baton2,
                               tb->p2,
                               scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 const char *left_file,
                 const char *right_file,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 svn_boolean_t file_modified,
                 const apr_array_header_t *prop_changes,
                 void *file_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_changed(relpath,
                               left_source,
                               right_source,
                               left_file,
                               right_file,
                               left_props,
                               right_props,
                               file_modified,
                               prop_changes,
                               fb->baton1,
                               tb->p1,
                               scratch_pool));

  SVN_ERR(tb->p2->file_changed(relpath,
                               left_source,
                               right_source,
                               left_file,
                               right_file,
                               left_props,
                               right_props,
                               file_modified,
                               prop_changes,
                               fb->baton2,
                               tb->p2,
                               scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tee_file_closed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *fb = file_baton;

  SVN_ERR(tb->p1->file_closed(relpath,
                              left_source,
                              right_source,
                              fb->baton1,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->file_closed(relpath,
                              left_source,
                              right_source,
                              fb->baton2,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tee_node_absent(const char *relpath,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct tee_baton_t *tb = processor->baton;
  struct tee_node_baton_t *db = dir_baton;

  SVN_ERR(tb->p1->node_absent(relpath,
                              db ? db->baton1 : NULL,
                              tb->p1,
                              scratch_pool));

  SVN_ERR(tb->p2->node_absent(relpath,
                              db ? db->baton2 : NULL,
                              tb->p2,
                              scratch_pool));

  return SVN_NO_ERROR;
}

const svn_diff_tree_processor_t *
svn_diff__tree_processor_tee_create(const svn_diff_tree_processor_t *processor1,
                                    const svn_diff_tree_processor_t *processor2,
                                    apr_pool_t *result_pool)
{
  struct tee_baton_t *tb = apr_pcalloc(result_pool, sizeof(*tb));
  svn_diff_tree_processor_t *tee;
  tb->p1 = processor1;
  tb->p2 = processor2;

  tee = svn_diff__tree_processor_create(tb, result_pool);

  tee->dir_opened    = tee_dir_opened;
  tee->dir_added     = tee_dir_added;
  tee->dir_deleted   = tee_dir_deleted;
  tee->dir_changed   = tee_dir_changed;
  tee->dir_closed    = tee_dir_closed;
  tee->file_opened   = tee_file_opened;
  tee->file_added    = tee_file_added;
  tee->file_deleted  = tee_file_deleted;
  tee->file_changed  = tee_file_changed;
  tee->file_closed   = tee_file_closed;
  tee->node_absent   = tee_node_absent;

  return tee;
}

svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool)
{
  svn_diff_source_t *src = apr_pcalloc(result_pool, sizeof(*src));

  src->revision = revision;
  return src;
}
