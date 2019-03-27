/*
 * default_editor.c -- provide a basic svn_delta_editor_t
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


#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_delta.h"


static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}
static svn_error_t *
add_item(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
single_baton_func(void *baton,
                  apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
absent_xxx_func(const char *path,
                void *baton,
                apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  *root_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
open_item(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
change_prop(void *file_baton,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *svn_delta_noop_window_handler(svn_txdelta_window_t *window,
                                           void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  *handler = svn_delta_noop_window_handler;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta_stream(const svn_delta_editor_t *editor,
                       void *file_baton,
                       const char *base_checksum,
                       svn_txdelta_stream_open_func_t open_func,
                       void *open_baton,
                       apr_pool_t *scratch_pool)
{
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  SVN_ERR(editor->apply_textdelta(file_baton, base_checksum,
                                  scratch_pool, &handler,
                                  &handler_baton));
  if (handler != svn_delta_noop_window_handler)
    {
      svn_txdelta_stream_t *txdelta_stream;

      SVN_ERR(open_func(&txdelta_stream, open_baton, scratch_pool,
                        scratch_pool));
      SVN_ERR(svn_txdelta_send_txstream(txdelta_stream, handler,
                                        handler_baton, scratch_pool));
    }

  return SVN_NO_ERROR;
}


static const svn_delta_editor_t default_editor =
{
  set_target_revision,
  open_root,
  delete_entry,
  add_item,
  open_item,
  change_prop,
  single_baton_func,
  absent_xxx_func,
  add_item,
  open_item,
  apply_textdelta,
  change_prop,
  close_file,
  absent_xxx_func,
  single_baton_func,
  single_baton_func,
  apply_textdelta_stream
};

svn_delta_editor_t *
svn_delta_default_editor(apr_pool_t *pool)
{
  return apr_pmemdup(pool, &default_editor, sizeof(default_editor));
}
