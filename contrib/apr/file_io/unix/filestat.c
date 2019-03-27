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

#include "apr_arch_file_io.h"
#include "apr_file_io.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "apr_errno.h"

#ifdef HAVE_UTIME
#include <utime.h>
#endif

static apr_filetype_e filetype_from_mode(mode_t mode)
{
    apr_filetype_e type;

    switch (mode & S_IFMT) {
    case S_IFREG:
        type = APR_REG;  break;
    case S_IFDIR:
        type = APR_DIR;  break;
    case S_IFLNK:
        type = APR_LNK;  break;
    case S_IFCHR:
        type = APR_CHR;  break;
    case S_IFBLK:
        type = APR_BLK;  break;
#if defined(S_IFFIFO)
    case S_IFFIFO:
        type = APR_PIPE; break;
#endif
#if !defined(BEOS) && defined(S_IFSOCK)
    case S_IFSOCK:
        type = APR_SOCK; break;
#endif

    default:
	/* Work around missing S_IFxxx values above
         * for Linux et al.
         */
#if !defined(S_IFFIFO) && defined(S_ISFIFO)
    	if (S_ISFIFO(mode)) {
            type = APR_PIPE;
	} else
#endif
#if !defined(BEOS) && !defined(S_IFSOCK) && defined(S_ISSOCK)
    	if (S_ISSOCK(mode)) {
            type = APR_SOCK;
	} else
#endif
        type = APR_UNKFILE;
    }
    return type;
}

static void fill_out_finfo(apr_finfo_t *finfo, struct_stat *info,
                           apr_int32_t wanted)
{ 
    finfo->valid = APR_FINFO_MIN | APR_FINFO_IDENT | APR_FINFO_NLINK
                 | APR_FINFO_OWNER | APR_FINFO_PROT;
    finfo->protection = apr_unix_mode2perms(info->st_mode);
    finfo->filetype = filetype_from_mode(info->st_mode);
    finfo->user = info->st_uid;
    finfo->group = info->st_gid;
    finfo->size = info->st_size;
    finfo->device = info->st_dev;
    finfo->nlink = info->st_nlink;

    /* Check for overflow if storing a 64-bit st_ino in a 32-bit
     * apr_ino_t for LFS builds: */
    if (sizeof(apr_ino_t) >= sizeof(info->st_ino)
        || (apr_ino_t)info->st_ino == info->st_ino) {
        finfo->inode = info->st_ino;
    } else {
        finfo->valid &= ~APR_FINFO_INODE;
    }

    apr_time_ansi_put(&finfo->atime, info->st_atime);
#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
    finfo->atime += info->st_atim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
    finfo->atime += info->st_atimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIME_N)
    finfo->atime += info->st_atime_n / APR_TIME_C(1000);
#endif

    apr_time_ansi_put(&finfo->mtime, info->st_mtime);
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
    finfo->mtime += info->st_mtim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
    finfo->mtime += info->st_mtimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
    finfo->mtime += info->st_mtime_n / APR_TIME_C(1000);
#endif

    apr_time_ansi_put(&finfo->ctime, info->st_ctime);
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
    finfo->ctime += info->st_ctim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
    finfo->ctime += info->st_ctimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIME_N)
    finfo->ctime += info->st_ctime_n / APR_TIME_C(1000);
#endif

#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
#ifdef DEV_BSIZE
    finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)DEV_BSIZE;
#else
    finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)512;
#endif
    finfo->valid |= APR_FINFO_CSIZE;
#endif
}

apr_status_t apr_file_info_get_locked(apr_finfo_t *finfo, apr_int32_t wanted,
                                      apr_file_t *thefile)
{
    struct_stat info;

    if (thefile->buffered) {
        apr_status_t rv = apr_file_flush_locked(thefile);
        if (rv != APR_SUCCESS)
            return rv;
    }

    if (fstat(thefile->filedes, &info) == 0) {
        finfo->pool = thefile->pool;
        finfo->fname = thefile->fname;
        fill_out_finfo(finfo, &info, wanted);
        return (wanted & ~finfo->valid) ? APR_INCOMPLETE : APR_SUCCESS;
    }
    else {
        return errno;
    }
}

APR_DECLARE(apr_status_t) apr_file_info_get(apr_finfo_t *finfo, 
                                            apr_int32_t wanted,
                                            apr_file_t *thefile)
{
    struct_stat info;

    if (thefile->buffered) {
        apr_status_t rv = apr_file_flush(thefile);
        if (rv != APR_SUCCESS)
            return rv;
    }

    if (fstat(thefile->filedes, &info) == 0) {
        finfo->pool = thefile->pool;
        finfo->fname = thefile->fname;
        fill_out_finfo(finfo, &info, wanted);
        return (wanted & ~finfo->valid) ? APR_INCOMPLETE : APR_SUCCESS;
    }
    else {
        return errno;
    }
}

APR_DECLARE(apr_status_t) apr_file_perms_set(const char *fname, 
                                             apr_fileperms_t perms)
{
    mode_t mode = apr_unix_perms2mode(perms);

    if (chmod(fname, mode) == -1)
        return errno;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_attrs_set(const char *fname,
                                             apr_fileattrs_t attributes,
                                             apr_fileattrs_t attr_mask,
                                             apr_pool_t *pool)
{
    apr_status_t status;
    apr_finfo_t finfo;

    /* Don't do anything if we can't handle the requested attributes */
    if (!(attr_mask & (APR_FILE_ATTR_READONLY
                       | APR_FILE_ATTR_EXECUTABLE)))
        return APR_SUCCESS;

    status = apr_stat(&finfo, fname, APR_FINFO_PROT, pool);
    if (status)
        return status;

    /* ### TODO: should added bits be umask'd? */
    if (attr_mask & APR_FILE_ATTR_READONLY)
    {
        if (attributes & APR_FILE_ATTR_READONLY)
        {
            finfo.protection &= ~APR_UWRITE;
            finfo.protection &= ~APR_GWRITE;
            finfo.protection &= ~APR_WWRITE;
        }
        else
        {
            /* ### umask this! */
            finfo.protection |= APR_UWRITE;
            finfo.protection |= APR_GWRITE;
            finfo.protection |= APR_WWRITE;
        }
    }

    if (attr_mask & APR_FILE_ATTR_EXECUTABLE)
    {
        if (attributes & APR_FILE_ATTR_EXECUTABLE)
        {
            /* ### umask this! */
            finfo.protection |= APR_UEXECUTE;
            finfo.protection |= APR_GEXECUTE;
            finfo.protection |= APR_WEXECUTE;
        }
        else
        {
            finfo.protection &= ~APR_UEXECUTE;
            finfo.protection &= ~APR_GEXECUTE;
            finfo.protection &= ~APR_WEXECUTE;
        }
    }

    return apr_file_perms_set(fname, finfo.protection);
}


APR_DECLARE(apr_status_t) apr_file_mtime_set(const char *fname,
                                              apr_time_t mtime,
                                              apr_pool_t *pool)
{
    apr_status_t status;
    apr_finfo_t finfo;

    status = apr_stat(&finfo, fname, APR_FINFO_ATIME, pool);
    if (status) {
        return status;
    }

#ifdef HAVE_UTIMES
    {
      struct timeval tvp[2];
    
      tvp[0].tv_sec = apr_time_sec(finfo.atime);
      tvp[0].tv_usec = apr_time_usec(finfo.atime);
      tvp[1].tv_sec = apr_time_sec(mtime);
      tvp[1].tv_usec = apr_time_usec(mtime);
      
      if (utimes(fname, tvp) == -1) {
        return errno;
      }
    }
#elif defined(HAVE_UTIME)
    {
      struct utimbuf buf;
      
      buf.actime = (time_t) (finfo.atime / APR_USEC_PER_SEC);
      buf.modtime = (time_t) (mtime / APR_USEC_PER_SEC);
      
      if (utime(fname, &buf) == -1) {
        return errno;
      }
    }
#else
    return APR_ENOTIMPL;
#endif

    return APR_SUCCESS;
}


APR_DECLARE(apr_status_t) apr_stat(apr_finfo_t *finfo, 
                                   const char *fname, 
                                   apr_int32_t wanted, apr_pool_t *pool)
{
    struct_stat info;
    int srv;

    if (wanted & APR_FINFO_LINK)
        srv = lstat(fname, &info);
    else
        srv = stat(fname, &info);

    if (srv == 0) {
        finfo->pool = pool;
        finfo->fname = fname;
        fill_out_finfo(finfo, &info, wanted);
        if (wanted & APR_FINFO_LINK)
            wanted &= ~APR_FINFO_LINK;
        return (wanted & ~finfo->valid) ? APR_INCOMPLETE : APR_SUCCESS;
    }
    else {
#if !defined(ENOENT) || !defined(ENOTDIR)
#error ENOENT || ENOTDIR not defined; please see the
#error comments at this line in the source for a workaround.
        /*
         * If ENOENT || ENOTDIR is not defined in one of the your OS's
         * include files, APR cannot report a good reason why the stat()
         * of the file failed; there are cases where it can fail even though
         * the file exists.  This opens holes in Apache, for example, because
         * it becomes possible for someone to get a directory listing of a 
         * directory even though there is an index (eg. index.html) file in 
         * it.  If you do not have a problem with this, delete the above 
         * #error lines and start the compile again.  If you need to do this,
         * please submit a bug report to http://www.apache.org/bug_report.html
         * letting us know that you needed to do this.  Please be sure to 
         * include the operating system you are using.
         */
        /* WARNING: All errors will be handled as not found
         */
#if !defined(ENOENT) 
        return APR_ENOENT;
#else
        /* WARNING: All errors but not found will be handled as not directory
         */
        if (errno != ENOENT)
            return APR_ENOENT;
        else
            return errno;
#endif
#else /* All was defined well, report the usual: */
        return errno;
#endif
    }
}


