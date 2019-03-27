/*
 * util.c:  Repository access utility routines.
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

/* ==================================================================== */

/*** Includes. ***/
#include <apr_pools.h>
#include <apr_network_io.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_ra.h"

#include "svn_private_config.h"
#include "private/svn_ra_private.h"

static const char *
get_path(const char *path_or_url,
         svn_ra_session_t *ra_session,
         apr_pool_t *pool)
{
  if (path_or_url == NULL)
    {
      svn_error_t *err = svn_ra_get_session_url(ra_session, &path_or_url,
                                                pool);
      if (err)
        {
          /* The SVN_ERR_UNSUPPORTED_FEATURE error in the caller is more
             important, so dummy up the session's URL and chuck this error. */
          svn_error_clear(err);
          return _("<repository>");
        }
    }
  return path_or_url;
}

svn_error_t *
svn_ra__assert_mergeinfo_capable_server(svn_ra_session_t *ra_session,
                                        const char *path_or_url,
                                        apr_pool_t *pool)
{
  svn_boolean_t mergeinfo_capable;
  SVN_ERR(svn_ra_has_capability(ra_session, &mergeinfo_capable,
                                SVN_RA_CAPABILITY_MERGEINFO, pool));
  if (! mergeinfo_capable)
    {
      path_or_url = get_path(path_or_url, ra_session, pool);
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Retrieval of mergeinfo unsupported by '%s'"),
                               svn_path_is_url(path_or_url)
                                  ? path_or_url
                                  : svn_dirent_local_style(path_or_url, pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__assert_capable_server(svn_ra_session_t *ra_session,
                              const char *capability,
                              const char *path_or_url,
                              apr_pool_t *pool)
{
  if (!strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO))
    return svn_ra__assert_mergeinfo_capable_server(ra_session, path_or_url,
                                                   pool);

  else
    {
      svn_boolean_t has;
      SVN_ERR(svn_ra_has_capability(ra_session, &has, capability, pool));
      if (! has)
        {
          path_or_url = get_path(path_or_url, ra_session, pool);
          return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("The '%s' feature is not supported by '%s'"),
                                 capability,
                                 svn_path_is_url(path_or_url)
                                    ? path_or_url
                                    : svn_dirent_local_style(path_or_url,
                                                             pool));
        }
    }
  return SVN_NO_ERROR;
}

/* Does ERR mean "the current value of the revprop isn't equal to
   the *OLD_VALUE_P you gave me"?
 */
static svn_boolean_t is_atomicity_error(svn_error_t *err)
{
  return svn_error_find_cause(err, SVN_ERR_FS_PROP_BASEVALUE_MISMATCH) != NULL;
}

svn_error_t *
svn_ra__release_operational_lock(svn_ra_session_t *session,
                                 const char *lock_revprop_name,
                                 const svn_string_t *mylocktoken,
                                 apr_pool_t *scratch_pool)
{
  svn_string_t *reposlocktoken;
  svn_boolean_t be_atomic;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                scratch_pool));
  SVN_ERR(svn_ra_rev_prop(session, 0, lock_revprop_name,
                          &reposlocktoken, scratch_pool));
  if (reposlocktoken && svn_string_compare(reposlocktoken, mylocktoken))
    {
      svn_error_t *err;

      err = svn_ra_change_rev_prop2(session, 0, lock_revprop_name,
                                    be_atomic ? &mylocktoken : NULL, NULL,
                                    scratch_pool);
      if (is_atomicity_error(err))
        {
          return svn_error_createf(err->apr_err, err,
                                   _("Lock was stolen by '%s'; unable to "
                                     "remove it"), reposlocktoken->data);
        }
      else
        SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__get_operational_lock(const svn_string_t **lock_string_p,
                             const svn_string_t **stolen_lock_p,
                             svn_ra_session_t *session,
                             const char *lock_revprop_name,
                             svn_boolean_t steal_lock,
                             int num_retries,
                             svn_ra__lock_retry_func_t retry_func,
                             void *retry_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *pool)
{
  char hostname_str[APRMAXHOSTLEN + 1] = { 0 };
  svn_string_t *mylocktoken, *reposlocktoken;
  apr_status_t apr_err;
  svn_boolean_t be_atomic;
  apr_pool_t *subpool;
  int i;

  *lock_string_p = NULL;
  if (stolen_lock_p)
    *stolen_lock_p = NULL;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS, pool));

  /* We build a lock token from the local hostname and a UUID.  */
  apr_err = apr_gethostname(hostname_str, sizeof(hostname_str), pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Unable to determine local hostname"));
  mylocktoken = svn_string_createf(pool, "%s:%s", hostname_str,
                                   svn_uuid_generate(pool));

  /* Ye Olde Retry Loope */
  subpool = svn_pool_create(pool);

  for (i = 0; i < num_retries; ++i)
    {
      svn_error_t *err;
      const svn_string_t *unset = NULL;

      svn_pool_clear(subpool);

      /* Check for cancellation.  If we're cancelled, don't leave a
         stray lock behind!  */
      if (cancel_func)
        {
          err = cancel_func(cancel_baton);
          if (err && err->apr_err == SVN_ERR_CANCELLED)
            return svn_error_compose_create(
                       svn_ra__release_operational_lock(session,
                                                        lock_revprop_name,
                                                        mylocktoken,
                                                        subpool),
                       err);
          SVN_ERR(err);
        }

      /* Ask the repository for the value of the LOCK_REVPROP_NAME. */
      SVN_ERR(svn_ra_rev_prop(session, 0, lock_revprop_name,
                              &reposlocktoken, subpool));

      /* Did we get a value from the repository?  We'll check to see
         if it matches our token.  If so, we call it success.  If not
         and we're told to steal locks, we remember the existing lock
         token and fall through to the locking code; othewise, we
         sleep and retry. */
      if (reposlocktoken)
        {
          if (svn_string_compare(reposlocktoken, mylocktoken))
            {
              *lock_string_p = mylocktoken;
              return SVN_NO_ERROR;
            }
          else if (! steal_lock)
            {
              if (retry_func)
                SVN_ERR(retry_func(retry_baton, reposlocktoken, subpool));
              apr_sleep(apr_time_from_sec(1));
              continue;
            }
          else
            {
              if (stolen_lock_p)
                *stolen_lock_p = svn_string_dup(reposlocktoken, pool);
              unset = reposlocktoken;
            }
        }

      /* No lock value in the repository, or we plan to steal it?
         Well, if we've got a spare iteration, we'll try to set the
         lock.  (We use the spare iteration to verify that we still
         have the lock after setting it.) */
      if (i < num_retries - 1)
        {
          /* Except in the very last iteration, try to set the lock. */
          err = svn_ra_change_rev_prop2(session, 0, lock_revprop_name,
                                        be_atomic ? &unset : NULL,
                                        mylocktoken, subpool);

          if (be_atomic && err && is_atomicity_error(err))
            {
              /* Someone else has the lock.  No problem, we'll loop again. */
              svn_error_clear(err);
            }
          else if (be_atomic && err == SVN_NO_ERROR)
            {
              /* Yay!  We have the lock!  However, for compatibility
                 with concurrent processes that don't support
                 atomicity, loop anyway to double-check that they
                 haven't overwritten our lock.
              */
              continue;
            }
          else
            {
              /* We have a genuine error, or aren't atomic and need
                 to loop.  */
              SVN_ERR(err);
            }
        }
    }

  return svn_error_createf(APR_EINVAL, NULL,
                           _("Couldn't get lock on destination repos "
                             "after %d attempts"), i);
}
