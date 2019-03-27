/*
 * compat.c :  Wrappers and callbacks for compatibility.
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

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_compat.h"
#include "svn_props.h"


/* Baton for use with svn_compat_wrap_commit_callback */
struct commit_wrapper_baton {
  void *baton;
  svn_commit_callback_t callback;
};

/* This implements svn_commit_callback2_t. */
static svn_error_t *
commit_wrapper_callback(const svn_commit_info_t *commit_info,
                        void *baton, apr_pool_t *pool)
{
  struct commit_wrapper_baton *cwb = baton;

  if (cwb->callback)
    return cwb->callback(commit_info->revision,
                         commit_info->date,
                         commit_info->author,
                         cwb->baton);

  return SVN_NO_ERROR;
}

void
svn_compat_wrap_commit_callback(svn_commit_callback2_t *callback2,
                                void **callback2_baton,
                                svn_commit_callback_t callback,
                                void *callback_baton,
                                apr_pool_t *pool)
{
  struct commit_wrapper_baton *cwb = apr_palloc(pool, sizeof(*cwb));

  /* Set the user provided old format callback in the baton */
  cwb->baton = callback_baton;
  cwb->callback = callback;

  *callback2_baton = cwb;
  *callback2 = commit_wrapper_callback;
}


void
svn_compat_log_revprops_clear(apr_hash_t *revprops)
{
  if (revprops)
    {
      svn_hash_sets(revprops, SVN_PROP_REVISION_AUTHOR, NULL);
      svn_hash_sets(revprops, SVN_PROP_REVISION_DATE, NULL);
      svn_hash_sets(revprops, SVN_PROP_REVISION_LOG, NULL);
    }
}

apr_array_header_t *
svn_compat_log_revprops_in(apr_pool_t *pool)
{
  apr_array_header_t *revprops = apr_array_make(pool, 3, sizeof(char *));

  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;
  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_DATE;
  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_LOG;

  return revprops;
}

void
svn_compat_log_revprops_out_string(const svn_string_t **author,
                                   const svn_string_t **date,
                                   const svn_string_t **message,
                                   apr_hash_t *revprops)
{
  *author = *date = *message = NULL;
  if (revprops)
    {
      *author = svn_hash_gets(revprops, SVN_PROP_REVISION_AUTHOR);
      *date = svn_hash_gets(revprops, SVN_PROP_REVISION_DATE);
      *message = svn_hash_gets(revprops, SVN_PROP_REVISION_LOG);
    }
}

void
svn_compat_log_revprops_out(const char **author, const char **date,
                            const char **message, apr_hash_t *revprops)
{
  const svn_string_t *author_s, *date_s,  *message_s;
  svn_compat_log_revprops_out_string(&author_s, &date_s,  &message_s,
                                     revprops);

  *author = author_s ? author_s->data : NULL;
  *date = date_s ? date_s->data : NULL;
  *message = message_s ? message_s->data : NULL;
}

/* Baton for use with svn_compat_wrap_log_receiver */
struct log_wrapper_baton {
  void *baton;
  svn_log_message_receiver_t receiver;
};

/* This implements svn_log_entry_receiver_t. */
static svn_error_t *
log_wrapper_callback(void *baton,
                     svn_log_entry_t *log_entry,
                     apr_pool_t *pool)
{
  struct log_wrapper_baton *lwb = baton;

  if (lwb->receiver && SVN_IS_VALID_REVNUM(log_entry->revision))
    {
      const char *author, *date, *message;
      svn_compat_log_revprops_out(&author, &date, &message,
                                  log_entry->revprops);
      return lwb->receiver(lwb->baton,
                           log_entry->changed_paths2,
                           log_entry->revision,
                           author, date, message,
                           pool);
    }

  return SVN_NO_ERROR;
}

void
svn_compat_wrap_log_receiver(svn_log_entry_receiver_t *receiver2,
                             void **receiver2_baton,
                             svn_log_message_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  struct log_wrapper_baton *lwb = apr_palloc(pool, sizeof(*lwb));

  /* Set the user provided old format callback in the baton. */
  lwb->baton = receiver_baton;
  lwb->receiver = receiver;

  *receiver2_baton = lwb;
  *receiver2 = log_wrapper_callback;
}
