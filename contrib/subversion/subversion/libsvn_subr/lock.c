/*
 * lock.c:  routines for svn_lock_t objects.
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

#include <apr_strings.h>

#include "svn_types.h"


/*** Code. ***/

svn_lock_t *
svn_lock_create(apr_pool_t *pool)
{
  return apr_pcalloc(pool, sizeof(svn_lock_t));
}

svn_lock_t *
svn_lock_dup(const svn_lock_t *lock, apr_pool_t *pool)
{
  svn_lock_t *new_l;

  if (lock == NULL)
    return NULL;

  new_l = apr_palloc(pool, sizeof(*new_l));
  *new_l = *lock;

  new_l->path = apr_pstrdup(pool, new_l->path);
  new_l->token = apr_pstrdup(pool, new_l->token);
  new_l->owner = apr_pstrdup(pool, new_l->owner);
  new_l->comment = apr_pstrdup(pool, new_l->comment);

  return new_l;
}
