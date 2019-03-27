/*
 * editor.c:  compatibility editors
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

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"

#include "private/svn_ra_private.h"
#include "private/svn_delta_private.h"
#include "private/svn_editor.h"

#include "ra_loader.h"
#include "svn_private_config.h"


struct fp_baton {
  svn_ra__provide_props_cb_t provide_props_cb;
  void *cb_baton;
};

struct fb_baton {
  svn_ra__provide_base_cb_t provide_base_cb;
  void *cb_baton;
};

/* The shims currently want a callback that provides props for a given
   REPOS_RELPATH at a given BASE_REVISION. However, the RA Ev2 interface
   has a callback that provides properties for the REPOS_RELPATH from any
   revision, which is returned along with the properties.

   This is a little shim to map between the prototypes. The base revision
   for the properties is discarded, and the requested revision (from the
   shim code) is ignored.

   The shim code needs to be updated to allow for an RA-style callback
   to fetch properties.  */
static svn_error_t *
fetch_props(apr_hash_t **props,
            void *baton,
            const char *repos_relpath,
            svn_revnum_t base_revision,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  struct fp_baton *fpb = baton;
  svn_revnum_t unused_revision;

  /* Ignored: BASE_REVISION.  */

  return svn_error_trace(fpb->provide_props_cb(props, &unused_revision,
                                               fpb->cb_baton,
                                               repos_relpath,
                                               result_pool, scratch_pool));
}

/* See note above regarding BASE_REVISION.
   This also pulls down the entire contents of the file stream from the
   RA layer and stores them in a local file, returning the path.
*/
static svn_error_t *
fetch_base(const char **filename,
           void *baton,
           const char *repos_relpath,
           svn_revnum_t base_revision,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  struct fb_baton *fbb = baton;
  svn_revnum_t unused_revision;
  svn_stream_t *contents;
  svn_stream_t *file_stream;
  const char *tmp_filename;

  /* Ignored: BASE_REVISION.  */

  SVN_ERR(fbb->provide_base_cb(&contents, &unused_revision, fbb->cb_baton,
                               repos_relpath, result_pool, scratch_pool));

  SVN_ERR(svn_stream_open_unique(&file_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, file_stream, NULL, NULL, scratch_pool));

  *filename = apr_pstrdup(result_pool, tmp_filename);



  return SVN_NO_ERROR;
}



svn_error_t *
svn_ra__use_commit_shim(svn_editor_t **editor,
                        svn_ra_session_t *session,
                        apr_hash_t *revprop_table,
                        svn_commit_callback2_t commit_callback,
                        void *commit_baton,
                        apr_hash_t *lock_tokens,
                        svn_boolean_t keep_locks,
                        svn_ra__provide_base_cb_t provide_base_cb,
                        svn_ra__provide_props_cb_t provide_props_cb,
                        svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                        void *cb_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *deditor;
  void *dedit_baton;
  struct svn_delta__extra_baton *exb;
  svn_delta__unlock_func_t unlock_func;
  void *unlock_baton;
  const char *repos_root;
  const char *session_url;
  const char *base_relpath;
  svn_boolean_t *found_abs_paths;
  struct fp_baton *fpb;

  /* NOTE: PROVIDE_BASE_CB is currently unused by this shim. In the future,
     we can pass it to the underlying Ev2/Ev1 shim to produce better
     apply_txdelta drives (ie. against a base rather than <empty>).  */

  /* Fetch the RA provider's Ev1 commit editor.  */
  SVN_ERR(session->vtable->get_commit_editor(session, &deditor, &dedit_baton,
                                             revprop_table,
                                             commit_callback, commit_baton,
                                             lock_tokens, keep_locks,
                                             result_pool));

  /* Get or calculate the appropriate repos root and base relpath. */
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root, scratch_pool));
  SVN_ERR(svn_ra_get_session_url(session, &session_url, scratch_pool));
  base_relpath = svn_uri_skip_ancestor(repos_root, session_url, scratch_pool);

  /* We will assume that when the underlying Ev1 editor is finally driven
     by the shim, that we will not need to prepend "/" to the paths. Place
     this on the heap because it is examined much later. Set to FALSE.  */
  found_abs_paths = apr_pcalloc(result_pool, sizeof(*found_abs_paths));

  /* The PROVIDE_PROPS_CB callback does not match what the shims want.
     Let's jigger things around a little bit here.  */
  fpb = apr_palloc(result_pool, sizeof(*fpb));
  fpb->provide_props_cb = provide_props_cb;
  fpb->cb_baton = cb_baton;

  /* Create the Ev2 editor from the Ev1 editor provided by the RA layer.

     Note: GET_COPYSRC_KIND_CB is compatible in type/semantics with the
     shim's FETCH_KIND_FUNC parameter.  */
  SVN_ERR(svn_delta__editor_from_delta(editor, &exb,
                                       &unlock_func, &unlock_baton,
                                       deditor, dedit_baton,
                                       found_abs_paths,
                                       repos_root, base_relpath,
                                       cancel_func, cancel_baton,
                                       get_copysrc_kind_cb, cb_baton,
                                       fetch_props, fpb,
                                       result_pool, scratch_pool));

  /* Note: UNLOCK_FUNC and UNLOCK_BATON are unused during commit drives.
     We can safely drop them on the floor.  */

  /* Since we're (currently) just wrapping an existing Ev1 editor, we have
     to call any start_edit handler it may provide (the shim uses this to
     invoke Ev1's open_root callback).  We've got a couple of options to do
     so: Implement a wrapper editor and call the start_edit callback upon
     the first invocation of any of the underlying editor's functions; or,
     just assume our consumer is going to eventually use the editor it is
     asking for, and call the start edit callback now.  For simplicity's
     sake, we do the latter.  */
  if (exb->start_edit)
    {
      /* Most commit drives pass SVN_INVALID_REVNUM for the revision.
         All calls to svn_delta_path_driver() pass SVN_INVALID_REVNUM,
         so this is fine for any commits done via that function.

         Notably, the PROPSET command passes a specific revision. Before
         PROPSET can use the RA Ev2 interface, we may need to make this
         revision a parameter.
         ### what are the exact semantics? what is the meaning of the
         ### revision passed to the Ev1->open_root() callback?  */
      SVN_ERR(exb->start_edit(exb->baton, SVN_INVALID_REVNUM));
    }

  /* Note: EXB also contains a TARGET_REVISION function, but that is not
     used during commit operations. We can safely ignore it. (ie. it is
     in EXB for use by paired-shims)  */

  return SVN_NO_ERROR;
}


struct wrapped_replay_baton_t {
  svn_ra__replay_revstart_ev2_callback_t revstart_func;
  svn_ra__replay_revfinish_ev2_callback_t revfinish_func;
  void *replay_baton;

  svn_ra_session_t *session;

  svn_ra__provide_base_cb_t provide_base_cb;
  svn_ra__provide_props_cb_t provide_props_cb;
  void *cb_baton;

  /* This will be populated by the revstart wrapper. */
  svn_editor_t *editor;
};

static svn_error_t *
revstart_func_wrapper(svn_revnum_t revision,
                      void *replay_baton,
                      const svn_delta_editor_t **deditor,
                      void **dedit_baton,
                      apr_hash_t *rev_props,
                      apr_pool_t *result_pool)
{
  struct wrapped_replay_baton_t *wrb = replay_baton;
  const char *repos_root;
  const char *session_url;
  const char *base_relpath;
  svn_boolean_t *found_abs_paths;
  struct fp_baton *fpb;
  struct svn_delta__extra_baton *exb;

  /* Get the Ev2 editor from the original revstart func. */
  SVN_ERR(wrb->revstart_func(revision, wrb->replay_baton, &wrb->editor,
                             rev_props, result_pool));

  /* Get or calculate the appropriate repos root and base relpath. */
  SVN_ERR(svn_ra_get_repos_root2(wrb->session, &repos_root, result_pool));
  SVN_ERR(svn_ra_get_session_url(wrb->session, &session_url, result_pool));
  base_relpath = svn_uri_skip_ancestor(repos_root, session_url, result_pool);

  /* We will assume that when the underlying Ev1 editor is finally driven
     by the shim, that we will not need to prepend "/" to the paths. Place
     this on the heap because it is examined much later. Set to FALSE.  */
  found_abs_paths = apr_pcalloc(result_pool, sizeof(*found_abs_paths));

  /* The PROVIDE_PROPS_CB callback does not match what the shims want.
     Let's jigger things around a little bit here.  */
  fpb = apr_palloc(result_pool, sizeof(*fpb));
  fpb->provide_props_cb = wrb->provide_props_cb;
  fpb->cb_baton = wrb->cb_baton;

  /* Create the extra baton. */
  exb = apr_pcalloc(result_pool, sizeof(*exb));

  /* Create the Ev1 editor from the Ev2 editor provided by the RA layer.

     Note: GET_COPYSRC_KIND_CB is compatible in type/semantics with the
     shim's FETCH_KIND_FUNC parameter.  */
  SVN_ERR(svn_delta__delta_from_editor(deditor, dedit_baton, wrb->editor,
                                       NULL, NULL,
                                       found_abs_paths,
                                       repos_root, base_relpath,
                                       fetch_props, wrb->cb_baton,
                                       fetch_base, wrb->cb_baton,
                                       exb, result_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
revfinish_func_wrapper(svn_revnum_t revision,
                       void *replay_baton,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       apr_hash_t *rev_props,
                       apr_pool_t *pool)
{
  struct wrapped_replay_baton_t *wrb = replay_baton;

  SVN_ERR(wrb->revfinish_func(revision, replay_baton, wrb->editor, rev_props,
                              pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__use_replay_range_shim(svn_ra_session_t *session,
                              svn_revnum_t start_revision,
                              svn_revnum_t end_revision,
                              svn_revnum_t low_water_mark,
                              svn_boolean_t send_deltas,
                              svn_ra__replay_revstart_ev2_callback_t revstart_func,
                              svn_ra__replay_revfinish_ev2_callback_t revfinish_func,
                              void *replay_baton,
                              svn_ra__provide_base_cb_t provide_base_cb,
                              svn_ra__provide_props_cb_t provide_props_cb,
                              void *cb_baton,
                              apr_pool_t *scratch_pool)
{
  /* The basic strategy here is to wrap the callback start and finish
     functions to appropriately return an Ev1 editor which is itself wrapped
     from the Ev2 one the provided callbacks will give us. */

  struct wrapped_replay_baton_t *wrb = apr_pcalloc(scratch_pool, sizeof(*wrb));

  wrb->revstart_func = revstart_func;
  wrb->revfinish_func = revfinish_func;
  wrb->replay_baton = replay_baton;
  wrb->session = session;

  wrb->provide_base_cb = provide_base_cb;
  wrb->provide_props_cb = provide_props_cb;
  wrb->cb_baton = cb_baton;

  return svn_error_trace(svn_ra_replay_range(session, start_revision,
                                             end_revision, low_water_mark,
                                             send_deltas,
                                             revstart_func_wrapper,
                                             revfinish_func_wrapper,
                                             wrb, scratch_pool));
}
