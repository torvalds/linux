/*
 * propdel-cmd.c -- Remove property from files/dirs
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propdel(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname;
  apr_array_header_t *args, *targets;

  /* Get the property's name (and a UTF-8 version of that name). */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname, pname, pool));
  /* No need to check svn_prop_name_is_valid for *deleting*
     properties, and it may even be useful to allow, in case invalid
     properties sneaked through somehow. */

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));


  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, pool);
  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, ctx, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_set2(pname, NULL, NULL,
                                      URL, &(opt_state->start_revision),
                                      &rev, FALSE, ctx, pool));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
               _("Cannot specify revision for deleting versioned property '%s'"),
               pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* For each target, remove the property PNAME. */
      SVN_ERR(svn_client_propset_local(pname, NULL, targets,
                                       opt_state->depth, FALSE,
                                       opt_state->changelists, ctx, pool));
    }

  return SVN_NO_ERROR;
}
