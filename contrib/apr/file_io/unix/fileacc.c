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

#include "apr_strings.h"
#include "apr_arch_file_io.h"

/* A file to put ALL of the accessor functions for apr_file_t types. */

APR_DECLARE(apr_status_t) apr_file_name_get(const char **fname,
                                           apr_file_t *thefile)
{
    *fname = thefile->fname;
    return APR_SUCCESS;
}

APR_DECLARE(apr_int32_t) apr_file_flags_get(apr_file_t *f)
{
    return f->flags;
}

#if !defined(OS2) && !defined(WIN32)
mode_t apr_unix_perms2mode(apr_fileperms_t perms)
{
    mode_t mode = 0;

    if (perms & APR_USETID)
        mode |= S_ISUID;
    if (perms & APR_UREAD)
        mode |= S_IRUSR;
    if (perms & APR_UWRITE)
        mode |= S_IWUSR;
    if (perms & APR_UEXECUTE)
        mode |= S_IXUSR;

    if (perms & APR_GSETID)
        mode |= S_ISGID;
    if (perms & APR_GREAD)
        mode |= S_IRGRP;
    if (perms & APR_GWRITE)
        mode |= S_IWGRP;
    if (perms & APR_GEXECUTE)
        mode |= S_IXGRP;

#ifdef S_ISVTX
    if (perms & APR_WSTICKY)
        mode |= S_ISVTX;
#endif
    if (perms & APR_WREAD)
        mode |= S_IROTH;
    if (perms & APR_WWRITE)
        mode |= S_IWOTH;
    if (perms & APR_WEXECUTE)
        mode |= S_IXOTH;

    return mode;
}

apr_fileperms_t apr_unix_mode2perms(mode_t mode)
{
    apr_fileperms_t perms = 0;

    if (mode & S_ISUID)
        perms |= APR_USETID;
    if (mode & S_IRUSR)
        perms |= APR_UREAD;
    if (mode & S_IWUSR)
        perms |= APR_UWRITE;
    if (mode & S_IXUSR)
        perms |= APR_UEXECUTE;

    if (mode & S_ISGID)
        perms |= APR_GSETID;
    if (mode & S_IRGRP)
        perms |= APR_GREAD;
    if (mode & S_IWGRP)
        perms |= APR_GWRITE;
    if (mode & S_IXGRP)
        perms |= APR_GEXECUTE;

#ifdef S_ISVTX
    if (mode & S_ISVTX)
        perms |= APR_WSTICKY;
#endif
    if (mode & S_IROTH)
        perms |= APR_WREAD;
    if (mode & S_IWOTH)
        perms |= APR_WWRITE;
    if (mode & S_IXOTH)
        perms |= APR_WEXECUTE;

    return perms;
}
#endif

APR_DECLARE(apr_status_t) apr_file_data_get(void **data, const char *key,
                                           apr_file_t *file)
{    
    return apr_pool_userdata_get(data, key, file->pool);
}

APR_DECLARE(apr_status_t) apr_file_data_set(apr_file_t *file, void *data,
                                           const char *key,
                                           apr_status_t (*cleanup)(void *))
{    
    return apr_pool_userdata_set(data, key, cleanup, file->pool);
}
