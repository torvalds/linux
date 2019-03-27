/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define APR_WANT_STRFUNC
#include "apr_want.h"
#include "apr.h"
#include "apr_private.h"
#include "apr_env.h"
#include "apr_strings.h"

#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

APR_DECLARE(apr_status_t) apr_env_get(char **value,
                                      const char *envvar,
                                      apr_pool_t *pool)
{
#ifdef HAVE_GETENV

    char *val = getenv(envvar);
    if (!val)
        return APR_ENOENT;
    *value = val;
    return APR_SUCCESS;

#else
    return APR_ENOTIMPL;
#endif
}


APR_DECLARE(apr_status_t) apr_env_set(const char *envvar,
                                      const char *value,
                                      apr_pool_t *pool)
{
#if defined(HAVE_SETENV)

    if (0 > setenv(envvar, value, 1))
        return APR_ENOMEM;
    return APR_SUCCESS;

#elif defined(HAVE_PUTENV)

    if (0 > putenv(apr_pstrcat(pool, envvar, "=", value, NULL)))
        return APR_ENOMEM;
    return APR_SUCCESS;

#else
    return APR_ENOTIMPL;
#endif
}


APR_DECLARE(apr_status_t) apr_env_delete(const char *envvar, apr_pool_t *pool)
{
#ifdef HAVE_UNSETENV

    unsetenv(envvar);
    return APR_SUCCESS;

#else
    /* hint: some platforms allow envvars to be unset via
     *       putenv("varname")...  that isn't Single Unix spec,
     *       but if your platform doesn't have unsetenv() it is
     *       worth investigating and potentially adding a
     *       configure check to decide when to use that form of
     *       putenv() here
     */
    return APR_ENOTIMPL;
#endif
}
