/* notify.c --- notifcation system
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "svn_pools.h"
#include "svn_repos.h"
#include "repos.h"
#include "svn_private_config.h"
#include "private/svn_utf_private.h"



svn_repos_notify_t *
svn_repos_notify_create(svn_repos_notify_action_t action,
                        apr_pool_t *result_pool)
{
  svn_repos_notify_t *notify = apr_pcalloc(result_pool, sizeof(*notify));

  notify->action = action;

  return notify;
}
