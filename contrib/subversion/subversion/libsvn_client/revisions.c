/*
 * revisions.c:  discovering revisions
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
#include "svn_ra.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"




svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_revnum_t *youngest_rev,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                apr_pool_t *scratch_pool)
{
  switch (revision->kind)
    {
    case svn_opt_revision_unspecified:
      *revnum = SVN_INVALID_REVNUM;
      break;

    case svn_opt_revision_number:
      *revnum = revision->value.number;
      break;

    case svn_opt_revision_head:
      /* If our caller provided a value for HEAD that he wants us to
         use, we'll use it.  Otherwise, we have to query the
         repository (and possible return our fetched value in
         *YOUNGEST_REV, too). */
      if (youngest_rev && SVN_IS_VALID_REVNUM(*youngest_rev))
        {
          *revnum = *youngest_rev;
        }
      else
        {
          if (! ra_session)
            return svn_error_create(SVN_ERR_CLIENT_RA_ACCESS_REQUIRED,
                                    NULL, NULL);
          SVN_ERR(svn_ra_get_latest_revnum(ra_session, revnum, scratch_pool));
          if (youngest_rev)
            *youngest_rev = *revnum;
        }
      break;

    case svn_opt_revision_working:
    case svn_opt_revision_base:
      {
        svn_error_t *err;

        /* Sanity check. */
        if (local_abspath == NULL)
          return svn_error_create(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
                                  NULL, NULL);

        /* The BASE, COMMITTED, and PREV revision keywords do not
           apply to URLs. */
        if (svn_path_is_url(local_abspath))
          return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                  _("PREV, BASE, or COMMITTED revision "
                                    "keywords are invalid for URL"));

        err = svn_wc__node_get_origin(NULL, revnum, NULL, NULL, NULL, NULL,
                                      NULL,
                                      wc_ctx, local_abspath, TRUE,
                                      scratch_pool, scratch_pool);

        /* Return the same error as older code did (before and at r935091).
           At least svn_client_proplist4 promises SVN_ERR_ENTRY_NOT_FOUND. */
        if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
          {
            svn_error_clear(err);
            return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                     _("'%s' is not under version control"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
          }
        else
          SVN_ERR(err);

        if (! SVN_IS_VALID_REVNUM(*revnum))
          return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                   _("Path '%s' has no committed "
                                     "revision"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
      }
      break;

    case svn_opt_revision_committed:
    case svn_opt_revision_previous:
      {
        /* Sanity check. */
        if (local_abspath == NULL)
          return svn_error_create(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
                                  NULL, NULL);

        /* The BASE, COMMITTED, and PREV revision keywords do not
           apply to URLs. */
        if (svn_path_is_url(local_abspath))
          return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                  _("PREV, BASE, or COMMITTED revision "
                                    "keywords are invalid for URL"));

        SVN_ERR(svn_wc__node_get_changed_info(revnum, NULL, NULL,
                                              wc_ctx, local_abspath,
                                              scratch_pool, scratch_pool));
        if (! SVN_IS_VALID_REVNUM(*revnum))
          return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                   _("Path '%s' has no committed "
                                     "revision"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));

        if (revision->kind == svn_opt_revision_previous)
          (*revnum)--;
      }
      break;

    case svn_opt_revision_date:
      /* ### When revision->kind == svn_opt_revision_date, is there an
         ### optimization such that we can compare
         ### revision->value->date with the committed-date in the
         ### entries file (or rather, with some range of which
         ### committed-date is one endpoint), and sometimes avoid a
         ### trip over the RA layer?  The only optimizations I can
         ### think of involve examining other entries to build a
         ### timespan across which committed-revision is known to be
         ### the head, but it doesn't seem worth it.  -kff */
      if (! ra_session)
        return svn_error_create(SVN_ERR_CLIENT_RA_ACCESS_REQUIRED, NULL, NULL);
      SVN_ERR(svn_ra_get_dated_revision(ra_session, revnum,
                                        revision->value.date, scratch_pool));
      break;

    default:
      return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                               _("Unrecognized revision type requested for "
                                 "'%s'"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  /* Final check -- if our caller provided a youngest revision, and
     the number we wound up with (after talking to the server) is younger
     than that revision, we need to stick to our caller's idea of "youngest".
   */
  if (youngest_rev
      && (revision->kind == svn_opt_revision_head
          || revision->kind == svn_opt_revision_date)
      && SVN_IS_VALID_REVNUM(*youngest_rev)
      && SVN_IS_VALID_REVNUM(*revnum)
      && (*revnum > *youngest_rev))
    *revnum = *youngest_rev;

  return SVN_NO_ERROR;
}
