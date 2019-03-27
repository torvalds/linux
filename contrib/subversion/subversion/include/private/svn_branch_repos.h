/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_branch_repos.h
 * @brief Operating on a repository
 *
 * @since New in ???.
 */


#ifndef SVN_BRANCH_REPOS_H
#define SVN_BRANCH_REPOS_H

#include <apr_pools.h>

#include "svn_types.h"

#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Create a new branching metadata object */
svn_branch__repos_t *
svn_branch__repos_create(apr_pool_t *result_pool);

/* Add REV_ROOT as the next revision in the repository REPOS.
 *
 * (This does not change the REV and BASE_REV fields of REV_ROOT. The
 * caller should set those, before or after this call.)
 */
svn_error_t *
svn_branch__repos_add_revision(svn_branch__repos_t *repos,
                               svn_branch__txn_t *rev_root);

/* Return a pointer to revision REVNUM of the repository REPOS.
 */
struct svn_branch__txn_t *
svn_branch__repos_get_revision(const svn_branch__repos_t *repos,
                               svn_revnum_t revnum);

/* Return the revision root that represents the base revision (or,
 * potentially, txn) of the revision or txn REV_ROOT.
 */
svn_branch__txn_t *
svn_branch__repos_get_base_revision_root(svn_branch__txn_t *rev_root);

/* Set *BRANCH_P to the branch found in REPOS : REVNUM : BRANCH_ID.
 *
 * Return an error if REVNUM or BRANCH_ID is not found.
 */
svn_error_t *
svn_branch__repos_get_branch_by_id(svn_branch__state_t **branch_p,
                                   const svn_branch__repos_t *repos,
                                   svn_revnum_t revnum,
                                   const char *branch_id,
                                   apr_pool_t *scratch_pool);

/* Set *EL_REV_P to the el-rev-id of the element at branch id BRANCH_ID,
 * element id EID, in revision REVNUM in REPOS.
 *
 * If there is no element there, set *EL_REV_P to point to an id in which
 * the BRANCH field is the nearest enclosing branch of RRPATH and the EID
 * field is -1.
 *
 * Allocate *EL_REV_P (but not the branch object that it refers to) in
 * RESULT_POOL.
 */
svn_error_t *
svn_branch__repos_find_el_rev_by_id(svn_branch__el_rev_id_t **el_rev_p,
                                    const svn_branch__repos_t *repos,
                                    svn_revnum_t revnum,
                                    const char *branch_id,
                                    int eid,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_REPOS_H */
