/* authz_info.c : Information derived from authz settings.
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

#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_hash.h"

#include "svn_private_config.h"

#include "authz.h"


svn_boolean_t
svn_authz__acl_applies_to_repo(const authz_acl_t *acl,
                               const char *repos)
{
  /* The repository name must match the one in the rule, iff the rule
     was defined for a specific repository. */
  return (0 == strcmp(acl->rule.repos, AUTHZ_ANY_REPOSITORY))
      || (0 == strcmp(repos, acl->rule.repos));
}

svn_boolean_t
svn_authz__get_acl_access(authz_access_t *access_p,
                          const authz_acl_t *acl,
                          const char *user, const char *repos)
{
  authz_access_t access;
  svn_boolean_t has_access;
  int i;

  /* The repository name must match the one in the rule, iff the rule
     was defined for a specific repository. */
  if (!svn_authz__acl_applies_to_repo(acl, repos))
    return FALSE;

  /* Check anonymous access first. */
  if (!user || 0 == strcmp(user, AUTHZ_ANONYMOUS_USER))
    {
      if (!acl->has_anon_access)
        return FALSE;

      if (access_p)
        *access_p = acl->anon_access;
      return TRUE;
    }

  /* Get the access rights for all authenticated users. */
  has_access = acl->has_authn_access;
  access = (has_access ? acl->authn_access : authz_access_none);

  /* Scan the ACEs in the ACL and merge the access rights. */
  for (i = 0; i < acl->user_access->nelts; ++i)
    {
      const authz_ace_t *const ace =
        &APR_ARRAY_IDX(acl->user_access, i, authz_ace_t);
      const svn_boolean_t match =
        ((ace->members && svn_hash_gets(ace->members, user))
         || (!ace->members && 0 == strcmp(user, ace->name)));

      if (!match != !ace->inverted) /* match XNOR ace->inverted */
        {
          access |= ace->access;
          has_access = TRUE;
        }
    }

  if (access_p)
    *access_p = access;
  return has_access;
}

/* Set *RIGHTS_P to the combination of LHS and RHS, i.e. intersect the
 * minimal rights and join the maximum rights.
 */
static void
combine_rights(authz_rights_t *rights_p,
               const authz_rights_t *lhs,
               const authz_rights_t *rhs)
{
  rights_p->min_access = lhs->min_access & rhs->min_access;
  rights_p->max_access = lhs->max_access | rhs->max_access;
}


/* Given GLOBAL_RIGHTS and a repository name REPOS, set *RIGHTS_P to
 * to the actual accumulated rights defined for that repository.
 * Return TRUE if these rights were defined explicitly.
 */
static svn_boolean_t
resolve_global_rights(authz_rights_t *rights_p,
                      const authz_global_rights_t *global_rights,
                      const char *repos)
{
  if (0 == strcmp(repos, AUTHZ_ANY_REPOSITORY))
    {
      /* Return the accumulated rights that are not repository-specific. */
      *rights_p = global_rights->any_repos_rights;
      return TRUE;
    }
  else
    {
      /* Check if we have explicit rights for this repository. */
      const authz_rights_t *const rights =
        svn_hash_gets(global_rights->per_repos_rights, repos);

      if (rights)
        {
          combine_rights(rights_p, rights, &global_rights->any_repos_rights);
          return TRUE;
        }
    }

  /* Fall-through: return the rights defined for "any" repository
     because this user has no specific rules for this specific REPOS. */
  *rights_p = global_rights->any_repos_rights;
  return FALSE;
}


svn_boolean_t
svn_authz__get_global_rights(authz_rights_t *rights_p,
                             const authz_full_t *authz,
                             const char *user, const char *repos)
{
  if (!user || 0 == strcmp(user, AUTHZ_ANONYMOUS_USER))
    {
      /* Check if we have explicit rights for anonymous access. */
      if (authz->has_anon_rights)
        return resolve_global_rights(rights_p, &authz->anon_rights, repos);
    }
  else
    {
      /* Check if we have explicit rights for this user. */
      const authz_global_rights_t *const user_rights =
        svn_hash_gets(authz->user_rights, user);

      if (user_rights)
        {
          svn_boolean_t explicit
            = resolve_global_rights(rights_p, user_rights, repos);

          /* Rights given to _any_ authenticated user may apply, too. */
          if (authz->has_authn_rights)
            {
              authz_rights_t authn;
              explicit |= resolve_global_rights(&authn, &authz->authn_rights,
                                                repos);
              combine_rights(rights_p, rights_p, &authn);
            }
          return explicit;
        }

      /* Check if we have explicit rights for authenticated access. */
      if (authz->has_authn_rights)
        return resolve_global_rights(rights_p, &authz->authn_rights, repos);
    }

  /* Fall-through: return the implicit rights, i.e., none. */
  rights_p->min_access = authz_access_none;
  rights_p->max_access = authz_access_none;
  return FALSE;
}
