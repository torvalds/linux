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

#ifndef INHERIT_H
#define INHERIT_H

#include "apr_inherit.h"

#define APR_INHERIT (1 << 24)    /* Must not conflict with other bits */

#define APR_IMPLEMENT_INHERIT_SET(name, flag, pool, cleanup)        \
apr_status_t apr_##name##_inherit_set(apr_##name##_t *the##name)    \
{                                                                   \
    if (the##name->flag & APR_FOPEN_NOCLEANUP)                      \
        return APR_EINVAL;                                          \
    if (!(the##name->flag & APR_INHERIT)) {                         \
        int flags = fcntl(the##name->name##des, F_GETFD);           \
        if (flags == -1)                                            \
            return errno;                                           \
        flags &= ~(FD_CLOEXEC);                                     \
        if (fcntl(the##name->name##des, F_SETFD, flags) == -1)      \
            return errno;                                           \
        the##name->flag |= APR_INHERIT;                             \
        apr_pool_child_cleanup_set(the##name->pool,                 \
                                   (void *)the##name,               \
                                   cleanup, apr_pool_cleanup_null); \
    }                                                               \
    return APR_SUCCESS;                                             \
}

#define APR_IMPLEMENT_INHERIT_UNSET(name, flag, pool, cleanup)      \
apr_status_t apr_##name##_inherit_unset(apr_##name##_t *the##name)  \
{                                                                   \
    if (the##name->flag & APR_FOPEN_NOCLEANUP)                      \
        return APR_EINVAL;                                          \
    if (the##name->flag & APR_INHERIT) {                            \
        int flags;                                                  \
        if ((flags = fcntl(the##name->name##des, F_GETFD)) == -1)   \
            return errno;                                           \
        flags |= FD_CLOEXEC;                                        \
        if (fcntl(the##name->name##des, F_SETFD, flags) == -1)      \
            return errno;                                           \
        the##name->flag &= ~APR_INHERIT;                            \
        apr_pool_child_cleanup_set(the##name->pool,                 \
                                   (void *)the##name,               \
                                   cleanup, cleanup);               \
    }                                                               \
    return APR_SUCCESS;                                             \
}

#endif	/* ! INHERIT_H */
