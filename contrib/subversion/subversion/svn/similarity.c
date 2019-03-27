/*
 * similarity.c: Utility functions for finding similar strings in lists
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

#include <stdlib.h>

#include "svn_string.h"
#include "cl.h"

#include "private/svn_string_private.h"

#include "svn_private_config.h"


/* Context for token similarity checking */
struct svn_cl__simcheck_context_t
{
  svn_string_t key;             /* The token we're comparing with */
  svn_membuf_t buffer;          /* Buffer for similarity testing */
};


/* Similarity test between two property names */
static APR_INLINE apr_size_t
simcheck_key_diff(const svn_string_t *key, const svn_string_t *ctx,
                 svn_membuf_t *buffer, apr_size_t *diff)
{
  apr_size_t lcs;
  const apr_size_t score = svn_string__similarity(key, ctx, buffer, &lcs);
  if (key->len > ctx->len)
    *diff = key->len - lcs;
  else
    *diff = ctx->len - lcs;
  return score;
}


/* Key comparator for qsort for svn_cl__simcheck_t */
static int
simcheck_compare(const void *pkeya, const void *pkeyb)
{
  svn_cl__simcheck_t *const keya = *(svn_cl__simcheck_t *const *)pkeya;
  svn_cl__simcheck_t *const keyb = *(svn_cl__simcheck_t *const *)pkeyb;
  svn_cl__simcheck_context_t *const context = keya->context;

  if (keya->score == -1)
    keya->score = simcheck_key_diff(&keya->token, &context->key,
                                    &context->buffer, &keya->diff);
  if (keyb->score == -1)
    keyb->score = simcheck_key_diff(&keyb->token, &context->key,
                                    &context->buffer, &keyb->diff);

  return (keya->score < keyb->score ? 1
          : (keya->score > keyb->score ? -1
             : (keya->diff > keyb->diff ? 1
                : (keya->diff < keyb->diff ? -1 : 0))));
}

apr_size_t
svn_cl__similarity_check(const char *key,
                         svn_cl__simcheck_t **tokens,
                         apr_size_t token_count,
                         apr_pool_t *scratch_pool)
{
  apr_size_t result;
  apr_size_t i;

  svn_cl__simcheck_context_t context;
  context.key.data = key;
  context.key.len = strlen(key);
  svn_membuf__create(&context.buffer, 0, scratch_pool);

  /* Populate the score, diff and context members. */
  for (i = 0; i < token_count; ++i)
    {
      svn_cl__simcheck_t *const token = tokens[i];
      token->score = -1;
      token->diff = 0;
      token->context = &context;
    }

  /* Sort the tokens by similarity. */
  qsort(tokens, token_count, sizeof(*tokens), simcheck_compare);

  /* Remove references to the context, since it points to the stack,
     and calculate the number of results that are at least two-thirds
     similar to the key. */
  for (i = 0, result = 1; i < token_count; ++i)
    {
      svn_cl__simcheck_t *const token = tokens[i];
      token->context = NULL;
      /* If you update this factor, consider updating
       * ../libsvn_subr/cmdline.c:most_similar(). */
      if (token->score >= (2 * SVN_STRING__SIM_RANGE_MAX + 1) / 3)
        ++result;
    }

  if (0 == tokens[0]->diff)
    return 0;                   /* We found an exact match. */
  return result;
}
